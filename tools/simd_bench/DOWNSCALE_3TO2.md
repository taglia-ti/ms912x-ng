# Fixed 3-to-2 downscale

Overscale reduces three source pixels to two output pixels on both axes.
Dimensions remain ceil-two-thirds, with adapter width aligned to four pixels:
1920×1080 becomes 1280×720; 1366×768 becomes 912×512. KMS and output timing
remain at the original resolution.

x86-64 uses Catmull-Rom in response to blurred text after reduction and
adapter upscaling. ARM32/ARM64 retain the lower-cost area filter. The scalar
fallback follows the architecture's filter, independent of SIMD availability.
See [ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md) for the native R3 comparison.

## ARM area filter

Each axis uses weights (2,1) and (1,2), choosing the weight-2 sample as near.
The output is `(4*a + 2*b + 2*c + d + 4) / 9` per byte, with one final
rounding, including X/alpha. NEON handles four outputs per vector, then two,
then scalar replicated edges. Structured three-way loads consume six source
pixels for each four-output group. Rounded Q15 multiply by 3641 implements
the division over the complete numerator range 0..2295. The scalar path
uses the identical area arithmetic.

This is a single pass with no temporary image or horizontal recomputation.
Both same-buffer and separate-buffer calls use it. Region entry points add
source-center offsets to preserve the global phase of staged damage crops.
FPU sections cover at most 16 output rows: 45 sections for 1080p input.
ARM glue remains integer-only; arithmetic helpers inline into the NEON worker.
Area retains the former softer contours; the performance change does not
preserve the cubic filter's output.

## x86-64 cubic phases and arithmetic

A centered destination sample maps to `1.5*(x+0.5)-0.5`. Its fractional source
phase is therefore 1/4 or 3/4. Catmull-Rom (B=0, C=1/2) has exact Q7 weights:

```text
phase 1/4:  [-9, 111,  29, -3] / 128
phase 3/4:  [-3,  29, 111, -9] / 128
```

The taps cover `floor(1.5*x)-1` through `floor(1.5*x)+2`. Source borders are
replicated, including adapter padding. Filtering is separable: round each
horizontal channel with `floor((sum+64)/128)`, clamp to 0..255, then perform
the vertical pass with the same rounding and clipping. All four byte channels,
including X/alpha, are processed. The 8-bit intermediate is intentional and
is part of the reference contract; this is not an unclipped 2D convolution
with only one final rounding. Stored channels are not gamma-linearized.

The negative lobes strengthen contours compared with the former positive
area weights. This can preserve apparent text contrast, but may also produce
halos/aliasing. There is no scale-expanded antialiasing prefilter. The result
cannot match native 1:1 detail, and visual improvement on the actual adapter
has not been established by numerical tests or the CPU benchmark.

The Catmull-Rom parameterization is also documented in
[Skia's sampling options](https://api.skia.org/structSkCubicResampler.html).
This implementation and its fixed-point/clipping decisions are local driver
code; it does not add a library dependency.

## x86-64 cubic scheduling and SIMD

`ms912x_downscale_template.h` shares scheduling between scalar, SSE2 and AVX2
backends. Arithmetic helpers are always inline; SIMD workers stay noinline
inside FPU guards. SIMD entry wrappers use integer registers only.

- SSE2 computes/stores four pixels per vector. Two exact 128-bit loads cover
  eight source pixels around each interior four-output group.
- AVX2 computes/stores eight pixels. Each 128-bit lane contains an independent
  four-output group. `VPMADDUBSW` combines unsigned samples with signed byte
  coefficients; the individual pair sums fit s16 without saturation.
- After subtracting the 16,384 DC bias, the signed numerator range is
  -19,444..19,316; adding 64 still fits s16. Reintroduce 128 after the arithmetic
  shift, then saturate to unsigned bytes. SSE2 uses widened biased samples;
  AVX2 subtracts the bias from partial sums to reduce instructions.
- No division, floating-point arithmetic, or branches on pixel content occur
  in the vector arithmetic. Scalar handles image boundaries. Control flow
  depends on geometry, pass selection and the last output row.

The actual full-frame call passes `slot->pixels` for both source and destination;
the cursor callers are also in place. Such calls first filter every source row
horizontally at the original stride, then filter vertical row pairs into the
output, including compact output strides. Source and destination must describe
valid shrinking images with adequate row storage. No heap allocation or full
image copy is added. Both output rows are computed before stores when they
may overlap source data. The first horizontal output pair is also read before
writing. Correctness checks include tiny images and packed output.

Separate-buffer calls compute horizontal results for five source rows shared
by each pair of destination rows, then apply both vertical phases. This avoids
allocating an intermediate image; some horizontal work is repeated between
row pairs. Kernel SIMD sections cover at most 16 rows of the current pass.
At 1080p in-place, this means 68 horizontal plus 45 vertical sections; the area
baseline used 45. The userspace benchmark excludes real FPU context costs.

## Damage staging

Conservatively expand source damage by two pixels per side on both
architectures, map by floor/ceil at 2/3, and
keep the start at an even output coordinate (four-pixel horizontal alignment
is retained). Staging copies include one leading source pixel/row and the
trailing support. Region entry points for both filters receive the source-center
offsets within this crop, avoiding artificial clamping at a partial-update boundary. The
region API is used with distinct staging and destination buffers; in-place
entry points use full images with zero offsets.

The existing on/off/auto eligibility policy and cursor coordinate mapping
remain at the same 1.5× ratio. Scan tolerance is unchanged by this experiment.

## Evidence

See [CUBIC_RESULTS.md](CUBIC_RESULTS.md) for the original cubic experiment and
[ARM_AREA_RESULTS.md](ARM_AREA_RESULTS.md) for the selected ARM area path,
native measurements, validation and limitations. The [text comparison](cubic-text-comparison.png)
uses the actual area and cubic driver filters, then a **simulated bilinear
upscale**, on grayscale DejaVu Sans Mono text at 12/14/16/20/24 pixels. It is
an algorithm illustration, not a capture or model identification of the chip's
upscaler. No UYVY conversion or subpixel rendering is included in that figure.
