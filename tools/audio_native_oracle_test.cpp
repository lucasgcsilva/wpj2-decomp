/* Executa uma AList capturada no Project64 pelo microcodigo RSP recompilado.
 * Diferente do harness HLE, este usa a mesma sequencia de Zelda64Recomp:
 * OSTask em DMEM 0xFC0, DMA de ucode_data em DMEM e chamada ao ucode real.
 * O objetivo e diagnostico; ele nunca participa do executavel do usuario. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int wpj2_native_audio_rsp(uint8_t* rdram, uint8_t* spmem);

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

static bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bool(f);
}

static bool read_ai_address(const std::string& path, uint32_t& address) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const std::string prefix = "AI buffer=0x";
        if (line.rfind(prefix, 0) == 0) {
            address = static_cast<uint32_t>(std::strtoul(line.c_str() + prefix.size(), nullptr, 16));
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : "temp/projeto/audio_deep_oracle/task_000300_full";
    const std::string sep = root.empty() || root.back() == '/' || root.back() == '\\' ? "" : "/";
    std::vector<uint8_t> logical_rdram, logical_task, expected;
    if (!read_file(root + sep + "rdram.bin", logical_rdram) || logical_rdram.size() != 0x800000) {
        std::fprintf(stderr, "ERRO: rdram.bin invalido.\n"); return 2;
    }
    if (!read_file(root + sep + "task.bin", logical_task) || logical_task.size() < 0x40) {
        std::fprintf(stderr, "ERRO: task.bin invalido.\n"); return 2;
    }
    if (!read_file(root + sep + "ai_pcm.bin", expected) || expected.empty()) {
        std::fprintf(stderr, "ERRO: ai_pcm.bin invalido.\n"); return 2;
    }
    uint32_t ai_address = 0;
    if (!read_ai_address(root + sep + "manifest.txt", ai_address)) {
        std::fprintf(stderr, "ERRO: AI buffer ausente no manifesto.\n"); return 2;
    }
    ai_address &= 0x1FFFFFFFu;
    if (ai_address + expected.size() > logical_rdram.size()) return 2;

    std::vector<uint8_t> host_rdram(logical_rdram.size());
    for (size_t i = 0; i < logical_rdram.size(); ++i) host_rdram[i ^ 3u] = logical_rdram[i];
    uint8_t spmem[0x1000]{};
    for (size_t i = 0; i < logical_task.size(); ++i) spmem[(0xFC0u + i) ^ 3u] = logical_task[i];

    const int result = wpj2_native_audio_rsp(host_rdram.data(), spmem);
    std::fprintf(stdout, "Microcodigo nativo retornou %d.\n", result);
    std::vector<uint8_t> actual(expected.size());
    for (size_t i = 0; i < actual.size(); ++i) actual[i] = host_rdram[(ai_address + i) ^ 3u];
    write_file(root + sep + "native_ai_pcm.bin", actual);
    std::vector<uint8_t> logical_after(host_rdram.size());
    for (size_t i = 0; i < logical_after.size(); ++i) logical_after[i] = host_rdram[i ^ 3u];
    write_file(root + sep + "native_rdram_after.bin", logical_after);
    return result == 0 ? 0 : 1;
}
