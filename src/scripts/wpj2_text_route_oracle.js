/* Wonder Project J2: rota nativa da cadeia decodificada ate o atlas CI8.
 * Execute no Project64 Debug/Interpreter antes de iniciar a ROM. A sonda nao
 * altera RAM, registradores, limite de frames ou plugins. */
'use strict';

var output = 'E:/projetos/project-wonder-j2-decomp/temp/oraculo/pj64-rdram/wpj2_text_route_oracle.tsv';
var pointerAddress = 0x80157864;
var generation = 0;
var budget = 0;
var calls = 0;

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
function printable(pointer) {
    var result = '', i, value;
    pointer = pointer >>> 0;
    if ((pointer & 0xE0000000) !== 0x80000000) return '';
    for (i = 0; i < 180; i++) {
        value = mem.u8[(pointer + i) >>> 0];
        if (value === 0) break;
        if (value === 0xE2 && i + 1 < 180) { i++; continue; }
        if (value === 10) result += '\\n';
        else if (value >= 0x20 && value <= 0x7E) result += String.fromCharCode(value);
        else return '';
    }
    return result;
}
function activePointer() { return mem.u32[pointerAddress] >>> 0; }
function record(kind, pc) {
    var pointer, text;
    if (budget <= 0) return;
    pointer = activePointer();
    text = printable(pointer);
    write(generation + '\t' + kind + '\t' + hex(pc, 8) + '\t' +
          hex(cpu.gpr.ra, 8) + '\t' + hex(cpu.gpr.a0, 8) + '\t' +
          hex(cpu.gpr.a1, 8) + '\t' + hex(cpu.gpr.a2, 8) + '\t' +
          hex(cpu.gpr.a3, 8) + '\t' + hex(pointer, 8) + '\t' + text);
    budget--;
    calls++;
}

var clear = fs.open(output, 'w');
fs.write(clear, 'geracao\tevento\tpc\tra\ta0\ta1\ta2\ta3\tponteiro\ttexto\\n\r\n');
fs.close(clear);

events.onwrite({ start: pointerAddress, end: pointerAddress + 4 }, function (e) {
    generation++;
    budget = 320;
    write(generation + '\tWRITE\t' + hex(e.pc, 8) + '\t' +
          hex(cpu.gpr.ra, 8) + '\t' + hex(cpu.gpr.a0, 8) + '\t' +
          hex(cpu.gpr.a1, 8) + '\t' + hex(cpu.gpr.a2, 8) + '\t' +
          hex(cpu.gpr.a3, 8) + '\t' + hex(activePointer(), 8) + '\t' +
          printable(activePointer()));
});

events.onexec(0x80090E58, function () { record('FORMAT_90E58', 0x80090E58); });
events.onexec(0x80095E78, function () { record('GLYPH_95E78', 0x80095E78); });
events.onexec(0x80095F9C, function () { record('GLYPH_95F9C', 0x80095F9C); });
events.onexec(0x80094230, function () { record('ATLAS_94230', 0x80094230); });

write('0\tSTART\t-\t-\t-\t-\t-\t-\t' + hex(activePointer(), 8) + '\t');
script.keepalive(true);
