#pragma once

#include <common.h>
#include <rlog.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace assets {

// One cached instance per (type, canonical path). Frame thread only; clear() before sg_shutdown().
class AssetLibrary {
    public:
        template <typename T>
        using Loader = std::function<T(const std::filesystem::path&)>;

        AssetLibrary() = default;
        // Split producers capture `this`: pinned in place.
        AssetLibrary(const AssetLibrary&) = delete;
        AssetLibrary& operator=(const AssetLibrary&) = delete;

        // Replaces any earlier loader for T. Receives the canonical absolute path.
        template <typename T>
        void setLoader(Loader<T> loader);

        // split(const Source&) -> std::tuple<Ts...>; a direct setLoader<Ti> takes precedence.
        template <typename Source, typename... Ts, typename Fn>
        void setSplitLoader(Fn&& split);

        // Shared, read-only. Fatal if no loader can produce T or the file doesn't exist.
        template <typename T>
        std::shared_ptr<const T> load(const std::filesystem::path& path) {
            return loadShared<T>(path);
        }

        // Same cached object as load<T>(), mutable: edits are visible to every holder.
        template <typename T>
        std::shared_ptr<T> loadMut(const std::filesystem::path& path) {
            return loadShared<T>(path);
        }

        template <typename T>
        bool contains(const std::filesystem::path& path) const {
            return m_cache.contains(Key{typeid(T), canonical(path)});
        }

        usize size() const { return m_cache.size(); }
        // Drops the library's references; objects die once callers drop theirs too.
        void clear() { m_cache.clear(); }

        static std::string canonical(const std::filesystem::path& path);

    private:
        struct Key {
            std::type_index type;
            std::string path;
            bool operator==(const Key&) const = default;
        };
        struct KeyHash {
            usize operator()(const Key& k) const {
                return std::hash<std::type_index>{}(k.type) ^ (std::hash<std::string>{}(k.path) * 31u);
            }
        };

        template <typename T>
        std::shared_ptr<T> loadShared(const std::filesystem::path& path);

        template <typename T>
        static std::string typeName() {
            return demangle(typeid(T).name());
        }
        static std::string demangle(const char* mangled);

        std::unordered_map<Key, std::shared_ptr<void>, KeyHash> m_cache;
        // type -> Loader<T>, type-erased
        std::unordered_map<std::type_index, std::shared_ptr<void>> m_loaders;
        // type -> loads the split source for a canonical path and caches all of its products
        std::unordered_map<std::type_index, std::function<void(const std::string&)>> m_split_producers;
};

template <typename T>
void AssetLibrary::setLoader(Loader<T> loader) {
    m_loaders[typeid(T)] = std::make_shared<Loader<T>>(std::move(loader));
}

template <typename Source, typename... Ts, typename Fn>
void AssetLibrary::setSplitLoader(Fn&& split) {
    static_assert(sizeof...(Ts) > 0, "setSplitLoader<Source, T1, ...>: list at least one product type");
    static_assert(std::is_same_v<std::invoke_result_t<Fn&, const Source&>, std::tuple<Ts...>>,
                  "setSplitLoader: split(const Source&) must return std::tuple<Ts...>");

    auto produce = [this, fn = std::function<std::tuple<Ts...>(const Source&)>(std::forward<Fn>(split))](const std::string& path) {
        const std::shared_ptr<Source> source = loadShared<Source>(path);
        std::tuple<Ts...> products = fn(*source);
        // try_emplace: a product already cached (e.g. via loadMut) keeps its identity.
        [&]<usize... I>(std::index_sequence<I...>) {
            (m_cache.try_emplace(Key{typeid(Ts), path}, std::make_shared<Ts>(std::move(std::get<I>(products)))), ...);
        }(std::index_sequence_for<Ts...>{});
    };
    const auto shared_produce = std::function<void(const std::string&)>(std::move(produce));
    ((m_split_producers[typeid(Ts)] = shared_produce), ...);
}

template <typename T>
std::shared_ptr<T> AssetLibrary::loadShared(const std::filesystem::path& path) {
    const std::string key_path = canonical(path);
    const Key key{typeid(T), key_path};

    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return std::static_pointer_cast<T>(it->second);
    }
    if (!std::filesystem::exists(key_path)) {
        RLOG(LL_FATAL, "assets: file not found: %s", key_path.c_str());
    }

    if (const auto it = m_loaders.find(typeid(T)); it != m_loaders.end()) {
        const Loader<T>& loader = *std::static_pointer_cast<Loader<T>>(it->second);
        std::shared_ptr<T> asset = std::make_shared<T>(loader(key_path));
        m_cache.emplace(key, asset);
        return asset;
    }
    if (const auto it = m_split_producers.find(typeid(T)); it != m_split_producers.end()) {
        it->second(key_path);
        return std::static_pointer_cast<T>(m_cache.at(key));
    }

    RLOG(LL_FATAL, "assets: no loader registered for %s (path %s)", typeName<T>().c_str(), key_path.c_str());
    __builtin_unreachable(); // RLOG(LL_FATAL, ...) exits the process
}

} // namespace assets
