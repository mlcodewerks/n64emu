"""Build unmodified nemu64 assertions, with an optional architecture-focused selection.

The checkout must be the pinned thelemmy/nemu64-test source. Dependencies and the
Rust toolchain are supplied by the caller; this script changes no global tools.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

REVISION = '9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c'
FOCUS = '''    result.retain(|test| {
        let name = test.name();
        name == "StartupTest" || name == "TearDownTest" ||
        name == "RSP SLTIU" || name == "RSP LWU" || name == "RSP STV" ||
        name.contains("64 bit") || name.contains("64-bit VA") ||
        name.starts_with("RE ") || name.starts_with("MI Repeat:") ||
        name.starts_with("cart_memory: DMA CART") ||
        name.starts_with("data cache:") || name.starts_with("icache: Cache(") ||
        name.starts_with("RDP STATUS:") || name == "rdram-regs: Read more"
    });
'''
ADDRESS_FOCUS = '''    result.retain(|test| {
        let name = test.name();
        name == "StartupTest" || name == "TearDownTest" ||
        ((name.contains("64 bit") || name.contains("64-bit VA")) &&
         !name.starts_with("Timing:") && !name.starts_with("64 bit with odd"))
    });
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkout', type=Path, required=True)
    parser.add_argument('--rust-bin', type=Path, required=True)
    parser.add_argument('--cargo-home', type=Path, required=True)
    parser.add_argument('--nust64', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--focus', nargs='?', const='all', choices=['all', 'addresses'],
                        help='Select the repaired cases, or isolate the full-address cases')
    parser.add_argument('--div-probe', action='store_true',
                        help='Build an observation ROM for physical DIV measurements, not a conformance test')
    args = parser.parse_args()
    if args.focus and args.div_probe:
        parser.error('--focus and --div-probe select different ROMs')
    output = args.output.resolve()
    source = output / 'source'
    shutil.copytree(args.checkout.resolve(), source, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('.git', 'target'))
    if args.div_probe:
        probe = Path(__file__).with_name('div_probe.rs.inc').read_text()
        path = source / 'src/tests/arithmetic/div.rs'
        path.write_text(path.read_text() + '\n' + probe)
        (source / 'src/tests/testlist.rs').write_text('''use alloc::boxed::Box;
use alloc::vec;
use alloc::vec::Vec;
use crate::tests::Test;
pub fn tests() -> Vec<Box<dyn Test>> {
    vec![Box::new(super::startup::StartupTest {}),
         Box::new(super::arithmetic::div::DivHardwareProbe {}),
         Box::new(super::startup::TearDownTest {})]
}
''')
    elif args.focus:
        path = source / 'src/tests/testlist.rs'
        text = path.read_text()
        marker = '    append_experimental_rdp_tests(&mut result);\n    result'
        if marker not in text:
            raise ValueError('unexpected upstream test list; review the selection before updating')
        selection = ADDRESS_FOCUS if args.focus == 'addresses' else FOCUS
        path.write_text(text.replace(marker, '    append_experimental_rdp_tests(&mut result);\n' + selection + '    result'))
    rust = args.rust_bin.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    env = dict(os.environ, RUSTC=str(rust / ('rustc' + suffix)),
               RUSTDOC=str(rust / ('rustdoc' + suffix)),
               CARGO_HOME=str(args.cargo_home.resolve()), CARGO_TARGET_DIR=str(output / 'target'))
    subprocess.run([str(rust / ('cargo' + suffix)), 'build', '--release', '--locked', '--offline',
                    '--features', 'timing,cycle,cop0hazard'], cwd=source, env=env, check=True)
    elf = output / 'target/mips-nintendo64-none/release/n64-systemtest'
    name = 'NEMU DIV PROBE' if args.div_probe else 'NEMU ARM FOCUS' if args.focus else 'NEMU64 ACCURATE'
    subprocess.run([str(args.nust64.resolve()), '--elf', str(elf), '--name', name], check=True)
    rom = output / ('nemu-div-probe.z64' if args.div_probe else 'nemu-arm-focus.z64' if args.focus else 'nemu-full.z64')
    shutil.copy2(elf.with_suffix('.z64'), rom)
    (output / 'build.json').write_text(json.dumps({
        'upstream': 'https://github.com/thelemmy/nemu64-test', 'revision': REVISION,
        'features': ['base', 'timing', 'cycle', 'cop0hazard'], 'focused_selection': args.focus or False,
        'div_observation_probe': args.div_probe,
        'rom_sha256': hashlib.sha256(rom.read_bytes()).hexdigest(),
        'cargo_lock_sha256': hashlib.sha256((source / 'Cargo.lock').read_bytes()).hexdigest(),
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
