// Physical-hardware observations only: no expected DIV outputs are asserted.
// Appended to the pinned nemu64 arithmetic::div module by prepare_nemu.py.
pub struct DivHardwareProbe {}

impl Test for DivHardwareProbe {
    fn name(&self) -> &str { "DIV hardware observations (not conformance)" }
    fn level(&self) -> Level { Level::BasicFunctionality }
    fn values(&self) -> Vec<Box<dyn Any>> { Vec::new() }
    fn run(&self, _value: &Box<dyn Any>) -> Result<(), String> {
        fn observe(a: u64, b: u64) {
            let lo: u64;
            let hi: u64;
            unsafe {
                asm!("
                    .set noat
                    .set noreorder
                    MFLO {old_lo}
                    MFHI {old_hi}
                    NOP
                    NOP
                    DIV $0, $2, $3
                    MFLO $2
                    MFHI $3
                    NOP
                    NOP
                    MTLO {old_lo}
                    MTHI {old_hi}
                ", inout("$2") a => lo, inout("$3") b => hi,
                    old_lo = out(reg) _, old_hi = out(reg) _);
            }
            crate::println!("DIVPROBE {:016x} {:016x} {:016x} {:016x}", a, b, lo, hi);
        }
        const INPUTS: [u64; 8] = [0, 1, 0x12, 0x7fff_ffff, 0x8000_0000,
            0xffff_ffff_ffff_ffff, 0xffff_ffff_0000_0012, 0xf604_788d_3ae9_b6fa];
        const DIVISORS: [u64; 12] = [0, 1, 2, 0xffff_ffff_ffff_fffe,
            0xffff_ffff_ffff_ffff, 0x8000_0000, 0xffff_fffe, 0xffff_ffff,
            0x8000_0000_0000_0000, 0x8000_0000_3333_3333,
            0xaa17_292a_425b_5b49, 0x60a7_1d05_ae80_701f];
        crate::println!("DIVPROBE BEGIN v1: columns RS RT LO HI; raw observations only");
        crate::println!("DIVPROBE PRID {:08x}", crate::cop0::previd());
        let mut rows = 0;
        for b in DIVISORS {
            for a in INPUTS { observe(a, b); rows += 1; }
        }
        for bit in 0..64 {
            for b in [0x8000_0000_0000_0000 | (1u64 << bit),
                      0x0000_0000_8000_0000 | (1u64 << bit)] {
                for a in INPUTS { observe(a, b); rows += 1; }
            }
        }
        crate::println!("DIVPROBE END rows={}", rows);
        Ok(())
    }
}
