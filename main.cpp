/*
Huffman Compressor / Decompressor (terminal-based)
Single-file C++ project suitable for a second-semester assignment.

Features:
- Compress/decompress text or files using Huffman coding.
- CLI modes:
    * Text mode:  -t "some text" -c    (compress text)
                  -t "some text" -d    (decompress text from stdin or file)
    * File mode:  -<ext> filename -c    (compress file; e.g. -txt mydoc.txt -c)
                  -<ext> filename -d    (decompress .huff file created by this program)
- Produces compressed files with suffix: .huff
- Header contains magic, original size, original extension, serialized Huffman tree
- Works on binary files (reads/writes bytes) so it supports arbitrary file types
- Simple "file-system" convenience: compressed files are written to ./compressed/ directory.

How to compile:
    g++ -std=c++17 -O2 -o huffman huffman_compressor.cpp

Examples:
    ./huffman -t "hello world" -c
    ./huffman -txt myfile.txt -c
    ./huffman -txt myfile.txt.huff -d

Notes:
- Not production hardened; meant for learning and demo.
- Tree serialization uses a pre-order traversal: '1' + byte for leaf, '0' for internal.

*/

#include <bits/stdc++.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

// --- Utility: ensure compressed dir exists ---
void ensure_compressed_dir() {
    string d = "compressed";
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    struct stat st = {0};
    if (stat(d.c_str(), &st) == -1) mkdir(d.c_str(), 0755);
#endif
}

// --- Huffman tree structures ---
struct Node {
    unsigned char ch;
    uint64_t freq;
    Node *l, *r;
    Node(unsigned char c, uint64_t f): ch(c), freq(f), l(nullptr), r(nullptr) {}
    Node(Node* L, Node* R): ch(0), freq(L->freq + R->freq), l(L), r(R) {}
};

struct Cmp {
    bool operator()(Node* a, Node* b) const {
        return a->freq > b->freq; // min-heap
    }
};

// --- Build Huffman tree from frequency table ---
Node* build_tree(const array<uint64_t,256>& freq) {
    priority_queue<Node*, vector<Node*>, Cmp> pq;
    for (int i = 0; i < 256; ++i) if (freq[i] > 0) pq.push(new Node((unsigned char)i, freq[i]));
    if (pq.empty()) return nullptr;
    // edge-case: only one symbol
    if (pq.size() == 1) {
        Node* only = pq.top(); pq.pop();
        Node* root = new Node(only, new Node((unsigned char)0, (uint64_t)0));
        return root;
    }
    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        pq.push(new Node(a,b));
    }
    return pq.top();
}

// --- Generate codes ---
void gen_codes(Node* node, string cur, array<string,256>& codes) {
    if (!node) return;
    if (!node->l && !node->r) {
        codes[node->ch] = cur.empty() ? "0" : cur; // handle single-symbol case
        return;
    }
    gen_codes(node->l, cur + '0', codes);
    gen_codes(node->r, cur + '1', codes);
}

// --- Serialize tree into bytes (pre-order): '1' + byte for leaf, '0' for internal ---
void serialize_tree(Node* node, vector<unsigned char>& out) {
    if (!node) return;
    if (!node->l && !node->r) {
        out.push_back('1');
        out.push_back(node->ch);
        return;
    }
    out.push_back('0');
    serialize_tree(node->l, out);
    serialize_tree(node->r, out);
}

// --- Deserialize tree from bytes, using index ref ---
Node* deserialize_tree(const vector<unsigned char>& in, size_t &idx) {
    if (idx >= in.size()) return nullptr;
    unsigned char t = in[idx++];
    if (t == '1') {
        unsigned char c = in[idx++];
        return new Node(c, 0);
    } else {
        Node* left = deserialize_tree(in, idx);
        Node* right = deserialize_tree(in, idx);
        return new Node(left, right);
    }
}

// --- Free tree memory ---
void free_tree(Node* n) {
    if (!n) return;
    free_tree(n->l);
    free_tree(n->r);
    delete n;
}

// --- Bit writer ---
struct BitWriter {
    ostream &os;
    unsigned char buffer = 0;
    int bitcount = 0;
    uint64_t total_bytes_written = 0;
    BitWriter(ostream &o): os(o) {}
    void push_bit(int b) {
        buffer = (buffer << 1) | (b & 1);
        bitcount++;
        if (bitcount == 8) {
            os.put((char)buffer);
            total_bytes_written++;
            buffer = 0; bitcount = 0;
        }
    }
    void push_bits(const string &bits) {
        for (char c: bits) push_bit(c == '1');
    }
    void flush() {
        if (bitcount > 0) {
            buffer <<= (8 - bitcount);
            os.put((char)buffer);
            total_bytes_written++;
            buffer = 0; bitcount = 0;
        }
    }
};

// --- Bit reader ---
struct BitReader {
    istream &is;
    unsigned char buffer = 0;
    int bitcount = 0;
    BitReader(istream &i): is(i) {}
    // return -1 on EOF
    int next_bit() {
        if (bitcount == 0) {
            int c = is.get();
            if (c == EOF) return -1;
            buffer = (unsigned char)c;
            bitcount = 8;
        }
        int b = (buffer >> 7) & 1;
        buffer <<= 1;
        bitcount--;
        return b;
    }
};

// --- Header format ---
// magic (4 bytes) 'H','U','F','1'
// orig_size (8 bytes little-endian) - uncompressed size in bytes
// ext_len (2 bytes) - length of original extension
// ext (ext_len bytes) - original extension string
// tree_size (4 bytes) - number of bytes of serialized tree
// tree (tree_size bytes)
// compressed_data follows

void write_uint64(ostream &os, uint64_t v) {
    for (int i = 0; i < 8; ++i) os.put((char)((v >> (8*i)) & 0xFF));
}
uint64_t read_uint64(istream &is) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        int c = is.get(); if (c == EOF) throw runtime_error("Unexpected EOF reading uint64");
        v |= (uint64_t)((unsigned char)c) << (8*i);
    }
    return v;
}

void write_uint32(ostream &os, uint32_t v) {
    for (int i = 0; i < 4; ++i) os.put((char)((v >> (8*i)) & 0xFF));
}
uint32_t read_uint32(istream &is) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        int c = is.get(); if (c == EOF) throw runtime_error("Unexpected EOF reading uint32");
        v |= (uint32_t)((unsigned char)c) << (8*i);
    }
    return v;
}

void write_uint16(ostream &os, uint16_t v) {
    for (int i = 0; i < 2; ++i) os.put((char)((v >> (8*i)) & 0xFF));
}
uint16_t read_uint16(istream &is) {
    uint16_t v = 0;
    for (int i = 0; i < 2; ++i) {
        int c = is.get(); if (c == EOF) throw runtime_error("Unexpected EOF reading uint16");
        v |= (uint16_t)((unsigned char)c) << (8*i);
    }
    return v;
}

// --- Compress byte vector to .huff file ---
void compress_bytes(const vector<unsigned char>& data, const string &orig_ext, const string &outpath) {
    array<uint64_t,256> freq{};
    for (unsigned char c: data) freq[c]++;
    Node* root = build_tree(freq);
    if (!root) {
        cerr << "Nothing to compress (empty input)\n";
        return;
    }
    array<string,256> codes;
    gen_codes(root, "", codes);

    vector<unsigned char> tree_bytes;
    serialize_tree(root, tree_bytes);

    // prepare output
    ofstream ofs(outpath, ios::binary);
    if (!ofs) { cerr << "Failed to open output: " << outpath << "\n"; free_tree(root); return; }
    // header
    ofs.write("HUF1", 4);
    write_uint64(ofs, data.size());
    uint16_t elen = (uint16_t)orig_ext.size();
    write_uint16(ofs, elen);
    ofs.write(orig_ext.data(), elen);
    write_uint32(ofs, (uint32_t)tree_bytes.size());
    if (!tree_bytes.empty()) ofs.write((char*)tree_bytes.data(), tree_bytes.size());

    // write compressed bits
    BitWriter bw(ofs);
    for (unsigned char c: data) {
        bw.push_bits(codes[c]);
    }
    bw.flush();
    free_tree(root);
    ofs.close();
    cout << "Wrote compressed file: " << outpath << "\n";
}

// --- Decompress .huff file to bytes ---
vector<unsigned char> decompress_file(const string &inpath, string &orig_ext) {
    ifstream ifs(inpath, ios::binary);
    if (!ifs) throw runtime_error("Failed to open input file");
    char magic[4]; ifs.read(magic,4);
    if (ifs.gcount() != 4 || strncmp(magic, "HUF1", 4) != 0) throw runtime_error("Not a .huff file or unsupported format");
    uint64_t orig_size = read_uint64(ifs);
    uint16_t elen = read_uint16(ifs);
    orig_ext.resize(elen);
    if (elen) ifs.read(&orig_ext[0], elen);
    uint32_t tree_size = read_uint32(ifs);
    vector<unsigned char> tree_bytes(tree_size);
    if (tree_size) ifs.read((char*)tree_bytes.data(), tree_size);

    size_t idx = 0;
    Node* root = deserialize_tree(tree_bytes, idx);
    vector<unsigned char> out; out.reserve(orig_size);

    BitReader br(ifs);
    Node* cur = root;
    while (out.size() < orig_size) {
        int b = br.next_bit();
        if (b == -1) break; // end of file
        if (b == 0) cur = cur->l; else cur = cur->r;
        if (!cur->l && !cur->r) {
            out.push_back(cur->ch);
            cur = root;
        }
    }
    free_tree(root);
    return out;
}

// --- Read file to bytes ---
vector<unsigned char> read_file_bytes(const string &path) {
    ifstream ifs(path, ios::binary);
    if (!ifs) throw runtime_error("Failed to open " + path);
    ifs.seekg(0, ios::end);
    size_t size = ifs.tellg();
    ifs.seekg(0, ios::beg);
    vector<unsigned char> buf(size);
    if (size) ifs.read((char*)buf.data(), size);
    return buf;
}

// --- Simple CLI parsing per user's requested style ---
void print_usage(const char* prog) {
    cerr << "Usage:\n";
    cerr << "  " << prog << " -t \"text to compress\" -c           (compress text)\n";
    cerr << "  " << prog << " -t \"text to decompress\" -d      (decompressing from .huff not supported for inline text)\n";
    cerr << "  " << prog << " -<ext> filename -c                (compress file; ext can be txt, png, bin etc)\n";
    cerr << "  " << prog << " -<ext> filename -d                (decompress .huff file created by this tool)\n";
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 3) { print_usage(argv[0]); return 1; }

    // find mode: -t or -<ext>
    string mode = argv[1];
    bool is_text = false;
    if (mode == string("-t")) is_text = true;

    if (is_text) {
        // expect: -t "some text" -c
        if (argc < 4) { print_usage(argv[0]); return 1; }
        string text = argv[2];
        string op = argv[3];
        if (op == "-c") {
            ensure_compressed_dir();
            vector<unsigned char> data(text.begin(), text.end());
            string outpath = string("compressed/text_") + to_string(time(nullptr)) + string(".huff");
            compress_bytes(data, string("txt"), outpath);
            return 0;
        } else {
            cerr << "Only -c supported for inline text in this simple app.\n";
            return 1;
        }
    } else {
        // file mode: argv[1] should be like -txt or -png etc. argv[2] filename. argv[3] operation -c or -d
        string extflag = mode; // e.g. -txt
        if (extflag.size() < 2 || extflag[0] != '-') { print_usage(argv[0]); return 1; }
        string ext = extflag.substr(1);
        string filename = argv[2];
        if (argc < 4) { print_usage(argv[0]); return 1; }
        string op = argv[3];

        if (op == "-c") {
            // compress
            vector<unsigned char> data;
            try { data = read_file_bytes(filename); }
            catch (const exception &e) { cerr << e.what() << "\n"; return 1; }
            ensure_compressed_dir();
            string outname = filename;
            // sanitize filename: remove path and dots maybe
            size_t pos = filename.find_last_of("/\\");
            string base = (pos==string::npos)? filename : filename.substr(pos+1);
            string outpath = string("compressed/") + base + string(".") + ext + string(".huff");
            compress_bytes(data, ext, outpath);
            return 0;
        } else if (op == "-d") {
            // decompress -- expecting a .huff file
            string inpath = filename;
            if (inpath.size() < 6 || inpath.substr(inpath.size()-5) != ".huff") {
                cerr << "Input for -d should be a .huff file produced by this program.\n";
                return 1;
            }
            try {
                string orig_ext;
                vector<unsigned char> raw = decompress_file(inpath, orig_ext);
                // write out to decompressed/ directory
                string outdir = "decompressed";
#ifdef _WIN32
                _mkdir(outdir.c_str());
#else
                struct stat st = {0};
                if (stat(outdir.c_str(), &st) == -1) mkdir(outdir.c_str(), 0755);
#endif
                // produce output filename
                string base = inpath;
                size_t pos = base.find_last_of("/\\");
                if (pos != string::npos) base = base.substr(pos+1);
                // remove .huff
                if (base.size() > 5 && base.substr(base.size()-5) == ".huff") base = base.substr(0, base.size()-5);
                string outname = outdir + "/" + base + string(".decoded") + (orig_ext.empty()? string("") : string(".") + orig_ext);
                ofstream ofs(outname, ios::binary);
                ofs.write((char*)raw.data(), raw.size());
                ofs.close();
                cout << "Decompressed to: " << outname << " (" << raw.size() << " bytes)\n";
            } catch (const exception &e) {
                cerr << "Error during decompress: " << e.what() << "\n";
                return 1;
            }
            return 0;
        } else {
            print_usage(argv[0]); return 1;
        }
    }

    return 0;
}
