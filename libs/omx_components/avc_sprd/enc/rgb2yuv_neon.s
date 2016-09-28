
@//this asm file is for fixed point IDCT

@ W1 2841 /* 2048*sqrt(2)*cos(1*pi/16) */
@ W2 2676 /* 2048*sqrt(2)*cos(2*pi/16) */
@ W3 2408 /* 2048*sqrt(2)*cos(3*pi/16) */
@ W5 1609 /* 2048*sqrt(2)*cos(5*pi/16) */
@ W6 1108 /* 2048*sqrt(2)*cos(6*pi/16) */
@ W7 565  /* 2048*sqrt(2)*cos(7*pi/16) */

@/************register map begin**************/
inrgb		.req		r0

outy		.req		r1
outuv		.req		r2

width_org	.req		r3
height_org	.req		r4

width_dst	.req		r5
height_dst	.req		r6

i		.req		r4
j		.req		r7

inrgb_n		.req		r8
outy_n		.req		r9

@/************register map end**************/

.equ		BLOCK_SIZE,	8

				.text
				.arm

@//fixed point IDCT
@//input: 8x8 block dct coefficient
@//	r0: input rgb's pointer,
@//	r1: point to output y frame
@//	r2: point to output uv frame
@//	r3: input width
@//	r4: input height
@//	r5: output width
@//	r6: output height

neon_intrinsics_ARGB888ToYVU420Semi:	@FUNCTION
				.global neon_intrinsics_ARGB888ToYVU420Semi

				push		{r4 - r12, r14}
				add		r8, sp, #40
				ldmia		r8, {height_org, width_dst, height_dst}

				ldr		r14, =rgb2yuv_coeff_neon

				ldmia		r14!,{r7, r8, r9, r10, r11, r12}
				vdup.8		d0, r7	@//r1
				vdup.8		d1, r8	@//g1
				vdup.8		d2, r9  @//b1
				vdup.8		d3, r10	@//r2
				vdup.8		d4, r11	@//g2
				vdup.8		d5, r12	@//b2, r3

				ldmia		r14, {r7, r8, r9, r10}
				vdup.8		d6, r7	@//g3
				vdup.8		d7, r8	@//b3
				vdup.8		d8, r9	@//y_base
				vdup.8		d9, r10	@//uv_base

loop_line:
				mov		j, width_org, lsr #3
loop_col_odd:
				vld4.8		{d10, d11, d12, d13}, [inrgb]!
				@//y
				vmull.u8	q10, d10, d0
				vmlal.u8	q10, d11, d1
				vmlal.u8	q10, d12, d2
				vshrn.u16	d30, q10, #8
				vadd.i8		d30, d30, d8
				vst1.u8		d30, [outy]!

				@//u,v
				vmull.u8	q10, d12, d5
				vmull.u8	q11, d10, d5

				vmlsl.u8	q10, d11, d4
				vmlsl.u8	q11, d11, d6

				vmlsl.u8	q10, d10, d3
				vmlsl.u8	q11, d12, d7
				vshrn.u16	d30, q10, #8
				vshrn.u16	d31, q11, #8

				vadd.i8		d30, d30, d9
				vadd.i8		d31, d31, d9

				vtrn.8		d31, d30
				vst1.u8		d30, [outuv]!

				subs		j, j, #1
				bgt		loop_col_odd

				mov		j, width_org, lsr #3
loop_col_even:
				vld4.8		{d10, d11, d12, d13}, [inrgb]!

				@//y
				vmull.u8	q10, d10, d0
				vmlal.u8	q10, d11, d1
				vmlal.u8	q10, d12, d2
				vshrn.u16	d30, q10, #8
				vadd.i8		d30, d30, d8
				vst1.u8		d30, [outy]!

				subs		j, j, #1
				bgt		loop_col_even

				subs		i, i, #2
				bgt		loop_line

				pop		{r4 - r12, pc}
				@ENDFUNC


				@AREA	DATA1, DATA, READONLY
				.align	4
rgb2yuv_coeff_neon:
				.word	66, 129, 25, 38, 74, 112, 94, 18, 16, 128

				.end


