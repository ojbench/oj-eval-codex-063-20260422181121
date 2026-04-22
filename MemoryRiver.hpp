// MemoryRiver.hpp - File-based storage with space reclamation
#ifndef BPT_MEMORYRIVER_HPP
#define BPT_MEMORYRIVER_HPP

#include <fstream>
#include <string>

using std::string;
using std::fstream;
using std::ifstream;
using std::ofstream;

template<class T, int info_len = 2>
class MemoryRiver {
private:
    fstream file;
    string file_name;
    int sizeofT = sizeof(T);

    // Sidecar recycle list file: stores count followed by that many int offsets
    string recycle_file_name() const { return file_name + .recycle; }

    void ensure_recycle_file() {
        std::fstream rf(recycle_file_name(), std::ios::in | std::ios::binary);
        if (!rf.is_open()) {
            // create and write count = 0
            std::fstream wf(recycle_file_name(), std::ios::out | std::ios::binary | std::ios::trunc);
            int zero = 0;
            wf.write(reinterpret_cast<char *>(&zero), sizeof(int));
            wf.close();
        } else {
            rf.close();
        }
    }

    // Pop a free offset from recycle list; return -1 if none
    int recycle_pop() {
        ensure_recycle_file();
        std::fstream rf(recycle_file_name(), std::ios::in | std::ios::out | std::ios::binary);
        if (!rf.is_open()) return -1;
        int cnt = 0;
        rf.seekg(0, std::ios::beg);
        rf.read(reinterpret_cast<char*>(&cnt), sizeof(int));
        if (cnt <= 0) {
            rf.close();
            return -1;
        }
        // read last offset
        long long pos = sizeof(int) + static_cast<long long>(cnt - 1) * sizeof(int);
        rf.seekg(pos, std::ios::beg);
        int off = -1;
        rf.read(reinterpret_cast<char*>(&off), sizeof(int));
        // decrement count
        --cnt;
        rf.seekp(0, std::ios::beg);
        rf.write(reinterpret_cast<char*>(&cnt), sizeof(int));
        rf.close();
        return off;
    }

    // Push a freed offset into recycle list
    void recycle_push(int index) {
        ensure_recycle_file();
        std::fstream rf(recycle_file_name(), std::ios::in | std::ios::out | std::ios::binary);
        if (!rf.is_open()) return;
        int cnt = 0;
        rf.seekg(0, std::ios::beg);
        rf.read(reinterpret_cast<char*>(&cnt), sizeof(int));
        long long pos = sizeof(int) + static_cast<long long>(cnt) * sizeof(int);
        rf.seekp(pos, std::ios::beg);
        rf.write(reinterpret_cast<char*>(&index), sizeof(int));
        ++cnt;
        rf.seekp(0, std::ios::beg);
        rf.write(reinterpret_cast<char*>(&cnt), sizeof(int));
        rf.close();
    }

public:
    MemoryRiver() = default;

    MemoryRiver(const string& file_name) : file_name(file_name) {}

    void initialise(string FN = ) {
        if (FN != ) file_name = FN;
        file.open(file_name, std::ios::out | std::ios::binary | std::ios::trunc);
        int tmp = 0;
        for (int i = 0; i < info_len; ++i)
            file.write(reinterpret_cast<char *>(&tmp), sizeof(int));
        file.close();
        // reset recycle sidecar
        ensure_recycle_file();
        std::fstream rf(recycle_file_name(), std::ios::out | std::ios::binary | std::ios::trunc);
        rf.write(reinterpret_cast<char*>(&tmp), sizeof(int));
        rf.close();
    }

	// 读出第n个int的值赋给tmp，1_base
    void get_info(int &tmp, int n) {
        if (n > info_len || n <= 0) return;
        std::ifstream fin(file_name, std::ios::in | std::ios::binary);
        if (!fin.is_open()) return;
        fin.seekg(static_cast<long long>(n - 1) * sizeof(int), std::ios::beg);
        fin.read(reinterpret_cast<char *>(&tmp), sizeof(int));
        fin.close();
    }

	// 将tmp写入第n个int的位置，1_base
    void write_info(int tmp, int n) {
        if (n > info_len || n <= 0) return;
        std::fstream fio(file_name, std::ios::in | std::ios::out | std::ios::binary);
        if (!fio.is_open()) return;
        fio.seekp(static_cast<long long>(n - 1) * sizeof(int), std::ios::beg);
        fio.write(reinterpret_cast<char *>(&tmp), sizeof(int));
        fio.close();
    }
    
	// 在文件合适位置写入类对象t，并返回写入的位置索引index
    int write(T &t) {
        // Try reuse a recycled slot
        int index = recycle_pop();
        std::fstream fio(file_name, std::ios::in | std::ios::out | std::ios::binary);
        if (!fio.is_open()) {
            // create file with header if missing
            initialise(file_name);
            fio.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (index < 0) {
            // append at end
            fio.seekp(0, std::ios::end);
            std::streampos endpos = fio.tellp();
            index = static_cast<int>(endpos);
            // if empty file, ensure header exists
            if (index < info_len * static_cast<int>(sizeof(int))) {
                fio.seekp(info_len * sizeof(int), std::ios::beg);
                index = info_len * sizeof(int);
            }
        } else {
            fio.seekp(index, std::ios::beg);
        }
        fio.write(reinterpret_cast<char *>(&t), sizeof(T));
        fio.close();
        return index;
    }
    
    // 用t的值更新位置索引index对应的对象
    void update(T &t, const int index) {
        std::fstream fio(file_name, std::ios::in | std::ios::out | std::ios::binary);
        if (!fio.is_open()) return;
        fio.seekp(index, std::ios::beg);
        fio.write(reinterpret_cast<char *>(&t), sizeof(T));
        fio.close();
    }

    // 读出位置索引index对应的T对象的值
    void read(T &t, const int index) {
        std::ifstream fin(file_name, std::ios::in | std::ios::binary);
        if (!fin.is_open()) return;
        fin.seekg(index, std::ios::beg);
        fin.read(reinterpret_cast<char *>(&t), sizeof(T));
        fin.close();
    }

    // 删除位置索引index对应的对象（回收空间）
    void Delete(int index) {
        // Optionally, zero-out is not necessary; just recycle
        recycle_push(index);
    }
};


#endif //BPT_MEMORYRIVER_HPP
