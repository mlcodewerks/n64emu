//
// arch/x86_64/rsp/vmov.c
//
// This file is subject to the terms and conditions defined in
// 'LICENSE', which is part of this source code package.
//

inline __m128i rsp_vmov(RSP::CPUState *rsp, unsigned src, unsigned e, unsigned dest, unsigned de)
{
	uint16_t data;

	// Get the element from VT.
	unsigned lane = de & 7;
	unsigned selected = e < 2 ? lane : e < 4 ? (lane & ~1u) | (e & 1) :
	    e < 8 ? (lane & ~3u) | (e & 3) : e & 7;
	data = rsp->cp2.regs[src].e[selected];

	// Write out the upper part of the result.
	rsp->cp2.regs[dest].e[de & 0x7] = data;
	return rsp_vect_load_unshuffled_operand(rsp->cp2.regs[dest].e);
}
