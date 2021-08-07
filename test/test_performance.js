const path = require("path");
const fs = require('fs');
const os = require('os');
const child_process = require("child_process");
const { promisify } = require("util");

const EXE_PATH = path.join(__dirname, '../build/syc');
const TEST_PATH = path.join(__dirname, './performance_test')
const TMP_DIR = fs.mkdtempSync(path.join(os.tmpdir(), 'syc-test-'));
const ASM_TMP_FILE = path.join(TMP_DIR, '${name}.s');
const EXE_TMP_FILE = path.join(TMP_DIR, '${name}.out');
const OPT = ['-O2'];
const WORKS = 22

let passCount = 0, failCount = 0;

process.on('SIGINT', () => {
    process.exit();
});
process.on('exit', () => {
    console.log();
    if (passCount > 0) console.log(`    \x1B[32m${passCount} pass\x1B[0m`);
    if (failCount > 0) console.log(`    \x1B[31m${failCount} fail\x1B[0m`);
    if (failCount === 0) fs.rmdirSync(TMP_DIR, { recursive: true });
});

async function doTest(path, name, opt = '-O0') {
    const exec = promisify(child_process.exec);
    let output = '', stderr = '', ans = '';
    let beginTime = new Date();
    try {
        await exec(`${EXE_PATH} ${path}.sy ${opt} -o "${ASM_TMP_FILE.replace('${name}', name + opt)}"`, { encoding: 'utf8' });
        await exec(`arm-linux-gnueabihf-gcc -march=armv7-a "${ASM_TMP_FILE.replace('${name}', name + opt)}" -L. -lsysy -o "${EXE_TMP_FILE.replace('${name}', name + opt)}" -static -g`, { encoding: 'utf8', cwd: __dirname });
        let exe = child_process.spawn('qemu-arm-static', [EXE_TMP_FILE.replace('${name}', name + opt)], { encoding: 'utf8' });
        try { exe.stdin.write(await fs.promises.readFile(path + '.in', { encoding: 'utf8' }) + '\n'); } catch (e) { }
        exe.stdout.on('data', (data) => {
            output += '' + data;
        });
        exe.stderr.on('data', (data) => {
            stderr += '' + data;
        });
        const timeout = setTimeout(() => {
            exe.kill();
            stderr += "timeout, killed by test.js"
        }, 300000);
        const code = await new Promise(resolve => {
            exe.on('exit', (code, signal) => resolve(code === null ? signal : code))
        });
        clearTimeout(timeout);
        await Promise.all([
            exe.stdout.readable ? new Promise(resolve => exe.stdout.once('close', resolve)) : null,
            exe.stderr.readable ? new Promise(resolve => exe.stderr.once('close', resolve)) : null,
        ])
        output = output.trim() + '\n' + code;
    } catch (e) {
        output += (e.stdout + '\n' + e.stderr) || JSON.stringify(e);
    }
    try {
        ans += await fs.promises.readFile(path + '.out', { encoding: 'utf8' });
    } catch (e) { }
    return {
        opt,
        pass: ans.trim() === output.trim(),
        expect: ans.trim(),
        get: output.trim(),
        stderr,
        time: new Date().getTime() - beginTime.getTime()
    };
}

(async function () {
    const tasks = [];
    const works = [];
    const dir = fs.readdirSync(TEST_PATH);
    for (const filePath of dir) {
        if (!/\.sy$/.test(filePath)) continue;
        const fullPath = path.join(TEST_PATH, filePath);
        tasks.push(fullPath.replace(/\.sy$/, ''));
    }
    for (let i = 0; i < WORKS; i++) {
        works.push((async function () {
            while (1) {
                if (tasks.length == 0) break;
                const p = tasks.shift();
                const tests = await Promise.all(OPT.map(opt => doTest(p, path.basename(p), opt)));
                if (tests.every(i => i.pass)) {
                    passCount++;
                    console.log(` \x1B[32m✓\x1B[0m ${p}.sy (${Math.max(...tests.map(i => i.time))}ms)`);
                } else {
                    failCount++;
                    console.log(` \x1B[31m✗ ${p}.sy\x1B[0m`);
                    for (const test of tests) {
                        if (!test.pass) {
                            console.log('  ' + test.opt);
                            console.log('    asm file: ' + ASM_TMP_FILE.replace('${name}', path.basename(p) + test.opt));
                            console.log('    expect:')
                            console.log(test.expect.split('\n').map(i => '      ' + i).join('\n'));
                            console.log('    get:')
                            console.log(test.get.split('\n').map(i => '      ' + i).join('\n'));
                            if (test.stderr.trim().length) {
                                console.log('    stderr:')
                                console.log(test.stderr.split('\n').map(i => '      ' + i).join('\n'));
                            }
                        }
                    }
                }
            }
        })())
    }
    await Promise.all(works);
    process.exit(failCount == 0 ? 0 : 1);
})();
