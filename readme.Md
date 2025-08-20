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