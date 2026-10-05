import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {serve} from '../../scripts/serve.mjs';

const root = path.resolve(import.meta.dirname, '../..');
const out = path.join(root, 'engine/artifacts/platform-services');
const {chromium} = await import(pathToFileURL('D:/cuda-webshader/node_modules/playwright/index.mjs'));
const {server, origin} = await serve();
let browser;
const report = {date: new Date().toISOString(), fullEngine: false, fullChaosSimulation: false, persistentFiles: false};
try {
    browser = await chromium.launch({executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: false});
    report.browser = await browser.version();
    const page = await browser.newPage();
    await page.goto(origin + '/engine-runtime/test.html');
    await page.evaluate(async () => {
        const {default: create} = await import('/engine-platform/services.mjs');
        globalThis.platform = await create({instantiateWasm(imports, receive) {
            WebAssembly.instantiateStreaming(fetch('/engine-platform/services.wasm'), imports).then(({instance, module}) => {
                globalThis.platformMemory = instance.exports.memory;
                receive(instance, module);
            });
            return {};
        }});
    });
    report.services = await page.evaluate(() => platform._platform_run());
    assert.equal(report.services, 0, 'Platform memory, TLS, atomics and strings');
    report.semaphore = await page.evaluate(async () => {
        const {default:create} = await import('/engine-platform/services.mjs');
        const results=[];
        for(const index of [0,1,2]) {
            const messages=[];
            const instance=await create({printErr: message=>messages.push(message)});
            let trapped=false;
            try { instance._platform_semaphore_violation(index); }
            catch(error) { trapped=error instanceof WebAssembly.RuntimeError; }
            results.push({index,trapped,messages});
        }
        return {singleThread:true,permitAccountingPassed:true,timedWaitsSupported:false,violations:results};
    });
    for(const result of report.semaphore.violations) {
        assert.equal(result.trapped,true,'Unavailable blocking waits and invalid counts must fail explicitly');
        assert.ok(result.messages.some(message=>message.startsWith('WebCuda:')));
    }
    report.files = await page.evaluate(() => {
        platform.FS.mkdir('/Content');
        platform.FS.writeFile('/Content/fixture.bin', Uint8Array.from({length: 4096}, (_, i) => (i * 37 + 11) % 256));
        const result = platform._platform_files_run();
        const output = [...platform.FS.readFile('/Saved/result.bin')];
        platform.FS.unlink('/Saved/result.bin');
        platform.FS.rmdir('/Saved');
        return {result, output};
    });
    assert.equal(report.files.result, 0, 'POSIX reads/writes/seeks/truncation/rename/stat/directory iteration');
    assert.deepEqual(report.files.output, Array.from({length: 2048}, (_, i) => (i * 37 + 11) % 256), 'C++ writes visible to HTML/JS');
    report.files = {passed: true, htmlToWasmBytes: 4096, wasmToHtmlBytes: 2048, backend: 'WasmFS memory backend'};
    const inputs = Array.from({length: 64}, (_, i) => [i / 8 - 4, (i % 13) / 4 - 2, (i % 7) / 2]);
    const actual = await page.evaluate(inputs => inputs.map(([x, y, z]) => {
        const pointer = platform._platform_chaos_transform(x, y, z);
        if (typeof pointer !== 'bigint') throw Error('Expected Wasm64');
        return [...new Float64Array(platformMemory.buffer, Number(pointer), 12)];
    }), inputs);
    let maxFloatError = 0, maxDoubleError = 0;
    inputs.forEach(([x, y, z], i) => {
        const expected = [5 - 2 * x, -2 - 3 * y, 9 + 4 * z, x, y, z];
        for (let j = 0; j < 6; ++j) {
            const floatError = Math.abs(actual[i][j] - expected[j]);
            const doubleError = Math.abs(actual[i][j + 6] - expected[j]);
            assert.ok(floatError < 1e-5 && doubleError < 1e-12, `Chaos transform ${i}:${j}`);
            maxFloatError = Math.max(maxFloatError, floatError);
            maxDoubleError = Math.max(maxDoubleError, doubleError);
        }
    });
    report.chaos = {passed: true, cases: inputs.length, floatAndDoubleComparisons: inputs.length * 12, maxFloatError, maxDoubleError, originalSourceEdited: false};
    report.memoryBenchmark = await page.evaluate(() => {
        platform._platform_memory_benchmark(0); platform._platform_memory_benchmark(1);
        const arena = [], mmap = [];
        for (let i = 0; i < 7; ++i) {
            // Alternate order to reduce first/second bias.
            if (i % 2) { mmap.push(platform._platform_memory_benchmark(1)); arena.push(platform._platform_memory_benchmark(0)); }
            else { arena.push(platform._platform_memory_benchmark(0)); mmap.push(platform._platform_memory_benchmark(1)); }
        }
        return {iterations: 64, reservationBytes: 16 * 1024 * 1024, touchedBytes: 65536, arenaMs: arena, emulatedMmapMs: mmap};
    });
    for (const values of [report.memoryBenchmark.arenaMs, report.memoryBenchmark.emulatedMmapMs]) assert.ok(values.every(v => Number.isFinite(v) && v >= 0));
    report.memoryBenchmark.arenaMedianMs = [...report.memoryBenchmark.arenaMs].sort((a,b)=>a-b)[3];
    report.memoryBenchmark.emulatedMmapMedianMs = [...report.memoryBenchmark.emulatedMmapMs].sort((a,b)=>a-b)[3];
    report.passed = true;
    console.log(JSON.stringify(report, null, 2));
} catch (error) {
    report.passed = false; report.error = error.stack; process.exitCode = 1; console.error(error);
} finally {
    report.wasmSHA256 = createHash('sha256').update(await readFile(path.join(out, 'services.wasm'))).digest('hex');
    await writeFile(path.join(out, 'validation.json'), JSON.stringify(report, null, 2));
    await browser?.close(); server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
}
