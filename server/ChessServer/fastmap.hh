#pragma once

template<class T, int size = 1 << 22> class fastmap {
    std::shared_ptr<T> table;
public:
    fastmap() {
        table = std::shared_ptr<T>(new T[size](), std::default_delete<T[]>());
    }

    inline T &operator[](size_t key) {
        return table.get()[key % size];
    }
};