/* Wonder Project J2: identifica a instrucao que preenche o buffer de dialogo.
 * Execute antes de iniciar/carregar a cena, com Interpreter core. O watchpoint
 * e instalado dinamicamente no v0 da alocacao em 0x800199A4. */
'use strict';

var output = 'E:/projetos/project-wonder-j2-decomp/temp/oraculo/pj64-rdram/wpj2_text_writer_oracle.tsv';
var allocation = 0;
var generation = 0;
var writes = 0;
var installed = {};

function hex(value, digits) {
    var text = (value >>> 0).toString(16).toUpperCase();
    while (text.length < digits) text = '0' + text;
    return '0x' + text;
}
function write(line) {
    var handle = fs.open(output, 'a');
    fs.write(handle, line + '\r\n');
    fs.close(handle);
}
function installWatch(base) {
    var key = hex(base, 8);
    if (installed[key]) return;
    installed[key] = true;
    /* 0x50 em diante recebe pixels/atlas e gerava milhares de eventos em
     * 8009088C. A cadeia decodificada comeca no offset zero. */
    events.onwrite({ start: base, end: (base + 0x50) >>> 0 }, function (e) {
        var pc = e.pc >>> 0;
        var ra = cpu.gpr.ra >>> 0;
        /* Eventos ja classificados: estado grafico, atlas e zero-fill inicial.
         * Eles se repetem milhares de vezes e escondiam o escritor tardio. */
        if (pc >= 0x80096380 && pc <= 0x80096460) return;
        if (pc === 0x8009088C || pc === 0x80019B7C || pc === 0x800199D4) return;
        if (pc === 0x800BD000 && ra === 0x800199F4) return;
        if (pc >= 0x800BC770 && pc <= 0x800BC830) return;
        if (pc === 0x8001A9DC || pc === 0x800137C0) return;
        if (writes >= 4096) return;
        writes++;
        write(generation + '\tWRITE_BUFFER\t' + hex(e.pc, 8) + '\t' +
              hex(e.address, 8) + '\t' + hex((e.address - base) >>> 0, 4) + '\t' +
              hex(ra, 8) + '\t' + hex(cpu.gpr.v0, 8) + '\t' +
              hex(cpu.gpr.v1, 8) + '\t' + hex(cpu.gpr.a0, 8) + '\t' +
              hex(cpu.gpr.a1, 8) + '\t' + hex(cpu.gpr.a2, 8) + '\t' +
              hex(cpu.gpr.a3, 8));
    });
    write(generation + '\tWATCH_INSTALLED\t0x800199A4\t' + key +
          '\t-\t' + hex(cpu.gpr.ra, 8) + '\t' + hex(cpu.gpr.v0, 8) +
          '\t' + hex(cpu.gpr.v1, 8) + '\t' + hex(cpu.gpr.a0, 8) + '\t' +
          hex(cpu.gpr.a1, 8) + '\t' + hex(cpu.gpr.a2, 8) + '\t' +
          hex(cpu.gpr.a3, 8));
}

function installFromActivePointer() {
    var base = mem.u32[0x80157864] >>> 0;
    if (((base & 0xFF800000) >>> 0) === 0x80000000) {
        if (generation === 0) generation = 1;
        installWatch(base);
    }
}

var clear = fs.open(output, 'w');
fs.write(clear, 'geracao\tevento\tpc\tendereco\toffset\tra\tv0\tv1\ta0\ta1\ta2\ta3\r\n');
fs.close(clear);

events.onexec(0x800199A4, function () {
    allocation = cpu.gpr.v0 >>> 0;
    generation++;
    if (((allocation & 0xFF800000) >>> 0) === 0x80000000)
        installWatch(allocation);
});

/* Load State restaura a RDRAM sem executar a instrucao de alocacao. O atlas
 * e uma fronteira frequente e segura para recuperar o ponteiro restaurado. */
events.onexec(0x80094230, function () { installFromActivePointer(); });

write('0\tSTART_POINTER\t-\t' + hex(mem.u32[0x80157864], 8) +
      '\t-\t-\t-\t-\t-\t-\t-\t-');
/* Rota preferencial: execute a sonda depois de carregar o save state. */
installFromActivePointer();
script.keepalive(true);
