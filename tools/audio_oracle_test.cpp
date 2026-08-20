/* Executa a AList NAUDIO capturada no Project64 pelo RSP recompilado e
 * compara o PCM produzido antes de qualquer API de audio do Windows.
 *
 * Uso: audio_oracle_test.exe <pasta-da-captura>
 * A captura deve conter rdram.bin (antes do RSP), task.bin e ai_pcm.bin
 * (depois do RSP). Os arquivos do Project64 estao em ordem logica do N64;
 * o runtime recompilado armazena RDRAM em ordem de palavras do host. */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int wpj2_native_audio_rsp(uint8_t* rdram, uint8_t* spmem);
extern "C" int wpj2_hle_audio_rsp(uint8_t* rdram, uint8_t* spmem);

/* O harness força o caminho HLE; este stub satisfaz a referência existente no
 * runtime sem incluir o microcódigo RSP ainda incompleto. */
extern "C" int wpj2_native_audio_rsp(uint8_t*, uint8_t*) { return -1; }
extern "C" uint64_t hle_retraces(void) { return 0; }

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

static int16_t read_be_s16(const uint8_t* p) {
    return static_cast<int16_t>((uint16_t(p[0]) << 8) | p[1]);
}

int main(int argc, char** argv) {
    const std::string root = argc >= 2 ? argv[1] : "analise/oraculo/audio/validacoes/task471";
    const std::string sep = root.empty() || root.back() == '/' || root.back() == '\\' ? "" : "/";
    std::vector<uint8_t> logical_rdram, logical_task, expected;
    if (!read_file(root + sep + "rdram.bin", logical_rdram) || logical_rdram.size() != 0x800000) {
        std::fprintf(stderr, "ERRO: rdram.bin ausente ou nao possui 8 MiB.\n"); return 2;
    }
    if (!read_file(root + sep + "task.bin", logical_task) || logical_task.size() < 0x40) {
        std::fprintf(stderr, "ERRO: task.bin ausente. Rode a sonda Project64 atualizada.\n"); return 2;
    }
    if (!read_file(root + sep + "ai_pcm.bin", expected) || expected.empty() || (expected.size() & 3)) {
        std::fprintf(stderr, "ERRO: ai_pcm.bin ausente ou invalido.\n"); return 2;
    }
    uint32_t ai_address = 0;
    if (!read_ai_address(root + sep + "manifest.txt", ai_address)) {
        std::fprintf(stderr, "ERRO: endereco do AI ausente no manifesto.\n"); return 2;
    }
    ai_address &= 0x1FFFFFFFu;
    if (ai_address + expected.size() > logical_rdram.size()) {
        std::fprintf(stderr, "ERRO: buffer AI fora da RDRAM: %08X + %zu.\n", ai_address, expected.size()); return 2;
    }

    std::vector<uint8_t> host_rdram(logical_rdram.size());
    for (size_t i = 0; i < logical_rdram.size(); ++i) host_rdram[i ^ 3] = logical_rdram[i];
    uint8_t spmem[0x1000]{};
    for (size_t i = 0; i < logical_task.size(); ++i) spmem[(0xFC0 + i) ^ 3] = logical_task[i];

    const int result = wpj2_hle_audio_rsp(host_rdram.data(), spmem);
    if (result != 0) {
        std::fprintf(stderr, "ERRO: RSP recompilado retornou %d.\n", result); return 3;
    }
    std::vector<uint8_t> actual(expected.size());
    for (size_t i = 0; i < actual.size(); ++i) actual[i] = host_rdram[(ai_address + i) ^ 3];
    write_file(root + sep + "hle_ai_pcm.bin", actual);
    std::vector<uint8_t> logical_after(host_rdram.size());
    for (size_t i = 0; i < logical_after.size(); ++i)
        logical_after[i] = host_rdram[i ^ 3];
    write_file(root + sep + "hle_rdram_after.bin", logical_after);

    size_t changed = 0, first = actual.size();
    double mse = 0.0, signal = 0.0;
    int peak_delta = 0;
    for (size_t i = 0; i < actual.size(); i += 2) {
        const int a = read_be_s16(actual.data() + i);
        const int b = read_be_s16(expected.data() + i);
        const int delta = a - b;
        if (actual[i] != expected[i] || actual[i + 1] != expected[i + 1]) {
            ++changed;
            if (first == actual.size()) first = i;
        }
        mse += double(delta) * delta;
        signal += double(b) * b;
        peak_delta = std::max(peak_delta, std::abs(delta));
    }
    const double samples = double(actual.size() / 2);
    const double rms_delta = std::sqrt(mse / samples);
    const double rms_expected = std::sqrt(signal / samples);
    std::printf("AI=0x%08X bytes=%zu frames=%zu\n", ai_address, actual.size(), actual.size() / 4);
    std::printf("PCM diferente: %zu/%zu amostras; primeira=%s; delta RMS=%.2f; sinal RMS=%.2f; pico=%d\n",
        changed, actual.size() / 2, first == actual.size() ? "nenhuma" : std::to_string(first).c_str(),
        rms_delta, rms_expected, peak_delta);
    std::printf("Saida HLE: %s\n", (root + sep + "hle_ai_pcm.bin").c_str());
    return changed == 0 ? 0 : 1;
}
