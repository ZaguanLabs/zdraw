/* Experimental screen-space raster surfaces. See docs/raster-experiment.md.
 * SPDX-License-Identifier: LicenseRef-Zsh
 * Distributed under the Zsh licence in LICENCE, as is zdraw.c.
 * Included after the cell writer and parameter-validation helpers.
 */
#define ZDRAW_RASTER_BYTES ((size_t)8 * 1024 * 1024)
#define ZDRAW_RASTER_PIXELS 65536
#define ZDRAW_RASTER_DIMENSION 512
#define ZDRAW_RASTER_PALETTE 32
#define ZDRAW_RASTER_SURFACES 32
#define ZDRAW_RASTER_TRIANGLES 4096
#define ZDRAW_RASTER_RECTANGLES (ZDRAW_RASTER_TRIANGLES / 2)
#define ZDRAW_RASTER_WORK 16777216
#define ZDRAW_RASTER_PLAN_CELLS 262144

struct zdraw_raster {
    struct zdraw_raster *next;
    char *name;
    int width, height, colors;
    int ink[ZDRAW_RASTER_PALETTE];
    char glyph[ZDRAW_RASTER_PALETTE];
    short pairs[ZDRAW_RASTER_PALETTE * ZDRAW_RASTER_PALETTE];
    int rgb_mode;
    unsigned int *rgb;
    double *depth;
    unsigned short *material;
    size_t bytes;
};

struct zdraw_triangle {
    double x[3], y[3], q[3], area;
    int material, left, right, top, bottom;
};

static struct zdraw_raster *zdraw_rasters;
static size_t zdraw_raster_bytes;
static int zdraw_raster_count;

static void
zdraw_raster_free(struct zdraw_raster *s)
{
    size_t pixels = (size_t)s->width * s->height;
    if (s->rgb) zfree(s->rgb, pixels * sizeof(*s->rgb));
    zfree(s->depth, pixels * sizeof(*s->depth));
    zfree(s->material, pixels * sizeof(*s->material));
    zsfree(s->name);
    zdraw_raster_bytes -= s->bytes;
    zdraw_raster_count--;
    zfree(s, sizeof(*s));
}

static void
zdraw_raster_cleanup(void)
{
    struct zdraw_raster *s;
    while ((s = zdraw_rasters)) {
        zdraw_rasters = s->next;
        zdraw_raster_free(s);
    }
}

/* Literal decimal only, independent of LC_NUMERIC and shell arithmetic.
 * A bounded mantissa and exponent keep parsing work bounded too. */
static int
zdraw_raster_number(const char *text, double *out, double bound)
{
    const unsigned char *p = (const unsigned char *)text;
    double value = 0;
    int negative = 0, digits = 0, fraction = 0, exponent = 0, eneg = 0;
    if (!*p || strlen(text) > 64) return 1;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p++ - '0');
        digits++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p++ - '0');
            digits++;
            fraction++;
        }
    }
    if (!digits) return 1;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '-' || *p == '+') eneg = *p++ == '-';
        if (*p < '0' || *p > '9') return 1;
        while (*p >= '0' && *p <= '9') {
            exponent = exponent * 10 + (*p++ - '0');
            if (exponent > 300) return 1;
        }
    }
    if (*p) return 1;
    exponent = (eneg ? -exponent : exponent) - fraction;
    while (exponent < 0) { value /= 10; exponent++; }
    while (exponent > 0) {
        if (value > bound) return 1;
        value *= 10;
        exponent--;
    }
    if (!(value <= bound)) return 1;
    *out = negative ? -value : value;
    return 0;
}

static int
zdraw_raster_size(char **args, int *width, int *height)
{
    return zdraw_nonnegative(args[0], width) ||
        zdraw_nonnegative(args[1], height) || !*width || !*height ||
        *width > ZDRAW_RASTER_DIMENSION || *height > ZDRAW_RASTER_DIMENSION ||
        *width > ZDRAW_RASTER_PIXELS / *height;
}

/* Resize clears to material zero and inverse depth zero. Reserve transient
 * old+new storage as well as retained storage against the same byte budget. */
static int
zdraw_raster_resize(struct zdraw_raster *s, int width, int height)
{
    size_t pixels = (size_t)width * height, i;
    size_t stride = sizeof(*s->depth) + sizeof(*s->material) +
        (s->rgb_mode ? sizeof(*s->rgb) : 0);
    size_t bytes = pixels * stride, oldpixels = (size_t)s->width * s->height;
    size_t old = oldpixels * stride;
    double *depth;
    unsigned short *material;
    unsigned int *rgb = NULL;
    if (bytes > ZDRAW_RASTER_BYTES - zdraw_raster_bytes) return 1;
    depth = (double *)zshcalloc(pixels * sizeof(*depth));
    if (!depth) return 1;
    material = (unsigned short *)zshcalloc(pixels * sizeof(*material));
    if (s->rgb_mode) rgb = (unsigned int *)zalloc(pixels * sizeof(*rgb));
    if (!material || (s->rgb_mode && !rgb)) {
        zfree(depth, pixels * sizeof(*depth));
        if (material) zfree(material, pixels * sizeof(*material));
        if (rgb) zfree(rgb, pixels * sizeof(*rgb));
        return 1;
    }
    if (rgb) for (i = 0; i < pixels; i++) rgb[i] = s->ink[0];
    zfree(s->depth, oldpixels * sizeof(*depth));
    zfree(s->material, oldpixels * sizeof(*material));
    if (s->rgb) zfree(s->rgb, oldpixels * sizeof(*rgb));
    s->depth = depth;
    s->material = material;
    s->rgb = rgb;
    s->width = width;
    s->height = height;
    s->bytes = s->bytes - old + bytes;
    zdraw_raster_bytes = zdraw_raster_bytes - old + bytes;
    return 0;
}

/* Storage accepts all RGB values, independent of terminal capabilities. */
static int
zdraw_raster_color(const char *text, unsigned int *out)
{
    unsigned int value = 0;
    int i, digit;
    if (strlen(text) != 7 || text[0] != '#') return 1;
    for (i = 1; i < 7; i++) {
        digit = text[i];
        if (digit >= '0' && digit <= '9') digit -= '0';
        else if (digit >= 'a' && digit <= 'f') digit -= 'a' - 10;
        else if (digit >= 'A' && digit <= 'F') digit -= 'A' - 10;
        else return 1;
        value = value * 16 + digit;
    }
    *out = value;
    return 0;
}

static unsigned int
zdraw_raster_channel(double value)
{
    return value <= 0 ? 0 : value >= 255 ? 255 : (unsigned int)(value + 0.5);
}

/* Inclusive pixel-center coverage, with the reference renderer's 1e-6
 * barycentric tolerance. Both windings are accepted; near clipping is caller
 * owned. Bounds are expanded one pixel to include the tolerance fringe. */
static void
zdraw_raster_bounds(struct zdraw_raster *s, struct zdraw_triangle *t)
{
    double xmin = t->x[0], xmax = xmin, ymin = t->y[0], ymax = ymin;
    int i;
    for (i = 1; i < 3; i++) {
        if (t->x[i] < xmin) xmin = t->x[i];
        if (t->x[i] > xmax) xmax = t->x[i];
        if (t->y[i] < ymin) ymin = t->y[i];
        if (t->y[i] > ymax) ymax = t->y[i];
    }
    t->left = xmin < 1 ? 0 : (int)xmin - 1;
    t->right = xmax >= s->width - 1 ? s->width - 1 : (int)xmax + 1;
    t->top = ymin < 1 ? 0 : (int)ymin - 1;
    t->bottom = ymax >= s->height - 1 ? s->height - 1 : (int)ymax + 1;
    t->area = (t->x[1]-t->x[0])*(t->y[2]-t->y[0]) -
              (t->y[1]-t->y[0])*(t->x[2]-t->x[0]);
    if (t->area >= -0.000001 && t->area <= 0.000001)
        t->right = -1;
}

static void
zdraw_raster_triangle(struct zdraw_raster *s, struct zdraw_triangle *t,
                      const unsigned int *rgb)
{
    double da, db, dq, a, b, q, px, py;
    int x, y, index, shift;
    if (t->left > t->right || t->top > t->bottom) return;
    da = (t->y[1] - t->y[2]) / t->area;
    db = (t->y[2] - t->y[0]) / t->area;
    dq = da*(t->q[0]-t->q[2]) + db*(t->q[1]-t->q[2]);
    px = t->left + 0.5;
    for (y = t->top; y <= t->bottom; y++) {
        py = y + 0.5;
        a = ((t->x[1]-px)*(t->y[2]-py) -
             (t->y[1]-py)*(t->x[2]-px)) / t->area;
        b = ((t->x[2]-px)*(t->y[0]-py) -
             (t->y[2]-py)*(t->x[0]-px)) / t->area;
        q = a*t->q[0] + b*t->q[1] + (1-a-b)*t->q[2];
        index = y*s->width + t->left;
        for (x = t->left; x <= t->right; x++, index++) {
            if (a >= -0.000001 && b >= -0.000001 &&
                1-a-b >= -0.000001 && q > s->depth[index]) {
                s->depth[index] = q;
                s->material[index] = (unsigned short)t->material;
                if (s->rgb) {
                    s->rgb[index] = s->ink[t->material];
                    if (rgb) {
                        unsigned int color = 0;
                        for (shift = 0; shift <= 16; shift += 8)
                            color |= zdraw_raster_channel(
                                a*((rgb[0] >> shift)&255) +
                                b*((rgb[1] >> shift)&255) +
                                (1-a-b)*((rgb[2] >> shift)&255)) << shift;
                        s->rgb[index] = color;
                    }
                }
            }
            a += da;
            b += db;
            q += dq;
        }
    }
}

static int
zdraw_raster_batch(struct zdraw_raster *s, char **args, int nargs, int colored)
{
    struct zdraw_triangle *batch, *t;
    size_t work = 0, area;
    int count, i, j, stride = colored ? 4 : 3, words = colored ? 13 : 10;
    unsigned int *rgb = NULL;
    if ((colored && !s->rgb) || nargs % words ||
        nargs / words > ZDRAW_RASTER_TRIANGLES) return 1;
    count = nargs / words;
    if (colored) rgb = (unsigned int *)zhalloc((size_t)count*3*sizeof(*rgb));
    batch = (struct zdraw_triangle *)zhalloc((size_t)count * sizeof(*batch));
    /* All validation precedes writes, including the total traversal budget. */
    for (i = 0; i < count; i++, args += words) {
        t = batch + i;
        for (j = 0; j < 3; j++) {
            if (zdraw_raster_number(args[j*stride], &t->x[j], 32768) ||
                zdraw_raster_number(args[j*stride+1], &t->y[j], 32768) ||
                zdraw_raster_number(args[j*stride+2], &t->q[j], 1000000) ||
                t->q[j] <= 0) return 1;
            if (colored && zdraw_raster_color(args[j*stride+3], rgb+i*3+j)) return 1;
        }
        if (zdraw_nonnegative(args[words-1], &t->material) ||
            t->material >= s->colors) return 1;
        zdraw_raster_bounds(s, t);
        if (t->left <= t->right && t->top <= t->bottom) {
            area = (size_t)(t->right-t->left+1)*(t->bottom-t->top+1);
            if (area > ZDRAW_RASTER_WORK - work) return 1;
            work += area;
        }
    }
    for (i = 0; i < count; i++)
        zdraw_raster_triangle(s, batch + i, colored ? rgb+i*3 : NULL);
    return 0;
}

/* Compact, axis-aligned input with the exact same two-triangle traversal.
 * Fixed diagonal: top-left -> bottom-right; first TL/TR/BR, then TL/BR/BL.
 * Keeping the triangle kernel preserves inclusive edges and rounding at depth
 * ties. This operation reduces submission/parsing work, not pixel visits. */
static int
zdraw_raster_rectangles(struct zdraw_raster *s, char **args, int nargs)
{
    struct zdraw_triangle *batch, *t;
    double v[6];
    size_t work = 0, area;
    int count, i, j, material;
    if (nargs % 7 || nargs / 7 > ZDRAW_RASTER_RECTANGLES) return 1;
    count = nargs / 7;
    batch = (struct zdraw_triangle *)zhalloc((size_t)count * 2 * sizeof(*batch));
    for (i = 0; i < count; i++, args += 7) {
        for (j = 0; j < 6; j++)
            if (zdraw_raster_number(args[j], v + j, j < 4 ? 32768 : 1000000) ||
                (j >= 4 && v[j] <= 0)) return 1;
        if (v[0] > v[2] || v[1] > v[3] ||
            zdraw_nonnegative(args[6], &material) || material >= s->colors)
            return 1;
        t = batch + i * 2;
        t[0].x[0] = t[1].x[0] = v[0];
        t[0].y[0] = t[1].y[0] = v[1];
        t[0].q[0] = t[1].q[0] = v[4];
        t[0].x[1] = v[2]; t[0].y[1] = v[1]; t[0].q[1] = v[4];
        t[0].x[2] = t[1].x[1] = v[2];
        t[0].y[2] = t[1].y[1] = v[3];
        t[0].q[2] = t[1].q[1] = v[5];
        t[1].x[2] = v[0]; t[1].y[2] = v[3]; t[1].q[2] = v[5];
        for (j = 0; j < 2; j++) {
            t[j].material = material;
            zdraw_raster_bounds(s, t + j);
            if (t[j].left <= t[j].right && t[j].top <= t[j].bottom) {
                area = (size_t)(t[j].right-t[j].left+1)*(t[j].bottom-t[j].top+1);
                if (area > ZDRAW_RASTER_WORK - work) return 1;
                work += area;
            }
        }
    }
    /* The complete batch, including both triangles' work, passed validation. */
    for (i = 0; i < count * 2; i++) zdraw_raster_triangle(s, batch + i, NULL);
    return 0;
}

static int
zdraw_raster_read(const char *nam, struct zdraw_raster *s, char *target, int rgb)
{
    Param pm;
    char **data, number[64];
    int i, pixels = s->width*s->height;
    if (rgb && !s->rgb) return 1;
    if (!isident(target) || strchr(target, '[')) return 1;
    pm = (Param)gethashnode2(paramtab, target);
    if (pm && ((pm->node.flags & (PM_READONLY|PM_SPECIAL)) ||
               PM_TYPE(pm->node.flags) != PM_ARRAY)) {
        zwarnnam(nam, "raster read expects an ordinary writable array");
        return 1;
    }
    data = (char **)zalloc((size_t)(pixels*2+1)*sizeof(*data));
    for (i = 0; i < pixels; i++) {
        if (rgb) sprintf(number, "#%06x", s->rgb[i]);
        else sprintf(number, "%u", (unsigned int)s->material[i]);
        data[i*2] = ztrdup(number);
        sprintf(number, "%.17g", s->depth[i]);
        data[i*2+1] = ztrdup(number);
    }
    data[pixels*2] = NULL;
    return !setaparam(target, data) || (errflag & ERRFLAG_ERROR);
}

/* Resolve into a new surface: area-weighted boxes in encoded RGB. Depth is
 * deliberately reset; a resolved image has no single geometric depth. */
static int
zdraw_raster_resolve(struct zdraw_raster *src, char **args)
{
    struct zdraw_raster *dst, *scan;
    int width, height, x, y, sx, sy, shift, index;
    size_t bytes;
    int x0, x1, y0, y1, wx, wy;
    unsigned long sum[3], area;
    if (!src->rgb || !isident(args[0]) || strchr(args[0], '[') ||
        strlen(args[0]) > 64 || zdraw_raster_size(args+1, &width, &height) ||
        width > src->width || height > src->height ||
        zdraw_raster_count >= ZDRAW_RASTER_SURFACES) return 1;
    for (scan = zdraw_rasters; scan; scan = scan->next)
        if (!strcmp(scan->name, args[0])) return 1;
    bytes = sizeof(*dst) + strlen(args[0]) + 1;
    if (bytes > ZDRAW_RASTER_BYTES-zdraw_raster_bytes) return 1;
    dst = (struct zdraw_raster *)zshcalloc(sizeof(*dst));
    if (!dst) return 1;
    dst->name = ztrdup(args[0]);
    dst->rgb_mode = 1;
    dst->colors = src->colors;
    memcpy(dst->ink, src->ink, sizeof(dst->ink));
    memcpy(dst->glyph, src->glyph, sizeof(dst->glyph));
    dst->bytes = bytes;
    zdraw_raster_bytes += bytes;
    zdraw_raster_count++;
    if (zdraw_raster_resize(dst, width, height)) {
        zdraw_raster_free(dst);
        return 1;
    }
    /* Integer overlap units avoid floating-point ambiguity at half-channel
     * rounding ties. The box area is always src width * src height <= 65536. */
    area = (unsigned long)src->width*src->height;
    for (y = 0; y < height; y++) {
        y0 = y*src->height;
        y1 = (y+1)*src->height;
        for (x = 0; x < width; x++) {
            x0 = x*src->width;
            x1 = (x+1)*src->width;
            sum[0] = sum[1] = sum[2] = 0;
            for (sy = y0/height; sy*height < y1; sy++) {
                wy = (y1 < (sy+1)*height ? y1 : (sy+1)*height) -
                     (y0 > sy*height ? y0 : sy*height);
                for (sx = x0/width; sx*width < x1; sx++) {
                    wx = (x1 < (sx+1)*width ? x1 : (sx+1)*width) -
                         (x0 > sx*width ? x0 : sx*width);
                    for (shift = 0; shift < 3; shift++)
                        sum[shift] += ((src->rgb[sy*src->width+sx] >> (shift*8))&255)*wx*wy;
                }
            }
            index = y*width+x;
            dst->rgb[index] = 0;
            for (shift = 0; shift < 3; shift++)
                dst->rgb[index] |= (unsigned int)((sum[shift]+area/2)/area) << (shift*8);
            /* The center sample selects only the ASCII/mono fallback glyph. */
            dst->material[index] = src->material[((y0+y1)/(2*height))*src->width+(x0+x1)/(2*width)];
        }
    }
    dst->next = zdraw_rasters;
    zdraw_rasters = dst;
    return 0;
}

struct zdraw_raster_plan {
    int requested, unique, reused, needed, max_pair, fits;
};

static int
zdraw_raster_pair_compare(const void *a, const void *b)
{
    return strcmp(*(const char * const *)a, *(const char * const *)b);
}

/* Canonical lowercase names match the shared immutable pair cache. No pair
 * allocation or legacy first-use state change occurs in this preflight. */
static int
zdraw_raster_plan(struct zdraw_raster **surfaces, int count, int half,
                  struct zdraw_raster_plan *plan)
{
    int i, x, y, n = 0, limit = zdraw_pair_limit();
    int path_limit = zdraw_spans_pair_limit();
    unsigned int upper, lower;
    char *names, **sorted;
    struct zdraw_raster *s;
    Colorpairnode pair;
    memset(plan, 0, sizeof(*plan));
    if (!zc_truecolor || !zdraw_colorpairs || path_limit < 0) return 2;
    for (i = 0; i < count; i++) {
        s = surfaces[i];
        if (!s->rgb) return 1;
        n += s->width*((s->height+1)/2);
        if (n > ZDRAW_RASTER_PLAN_CELLS) return 1;
    }
    names = (char *)zhalloc((size_t)n*16);
    sorted = (char **)zhalloc((size_t)n*sizeof(*sorted));
    plan->requested = n;
    n = 0;
    for (i = 0; i < count; i++) {
        s = surfaces[i];
        for (y = 0; y < s->height; y += 2) {
            for (x = 0; x < s->width; x++) {
                upper = s->rgb[y*s->width+x];
                lower = half && y+1 < s->height ? s->rgb[(y+1)*s->width+x] : (unsigned int)s->ink[0];
                if (upper < (unsigned int)zc_rgb_min || lower < (unsigned int)zc_rgb_min)
                    return 2;
                sorted[n] = names+n*16;
                sprintf(sorted[n++], "#%06x/#%06x", upper, lower);
            }
        }
    }
    qsort(sorted, n, sizeof(*sorted), zdraw_raster_pair_compare);
    for (i = 0; i < n; i++) {
        if (i && !strcmp(sorted[i], sorted[i-1])) continue;
        plan->unique++;
        pair = (Colorpairnode)gethashnode2(zdraw_colorpairs, sorted[i]);
        if (pair) {
            plan->reused++;
            if (pair->colorpair > plan->max_pair) plan->max_pair = pair->colorpair;
        } else plan->needed++;
    }
    if (plan->needed && next_cp+plan->needed > plan->max_pair)
        plan->max_pair = next_cp+plan->needed;
    plan->fits = plan->needed <= limit-next_cp && plan->max_pair <= path_limit;
    return 0;
}

static int
zdraw_raster_plan_command(const char *nam, struct zdraw_raster *first, char **args, int nargs)
{
    struct zdraw_raster *surfaces[ZDRAW_RASTER_SURFACES], *s;
    struct zdraw_raster_plan plan;
    int i, result;
    LinkList info;
    if (!zdraw_getwindowbyname("stdscr") || nargs < 1 ||
        nargs > ZDRAW_RASTER_SURFACES || zdraw_association(nam, args[0])) return 1;
    surfaces[0] = first;
    for (i = 1; i < nargs; i++) {
        for (s = zdraw_rasters; s && strcmp(s->name, args[i]); s = s->next);
        if (!s) return 1;
        surfaces[i] = s;
    }
    result = zdraw_raster_plan(surfaces, nargs, 1, &plan);
    if (result) return result;
    info = newlinklist();
    zdraw_colorinfo_value(info, "requested", plan.requested);
    zdraw_colorinfo_value(info, "unique", plan.unique);
    zdraw_colorinfo_value(info, "pairs_reused", plan.reused);
    zdraw_colorinfo_value(info, "pairs_needed", plan.needed);
    zdraw_colorinfo_value(info, "pairs_used", next_cp);
    zdraw_colorinfo_value(info, "pairs_free", zdraw_pair_limit()-next_cp);
    zdraw_colorinfo_value(info, "pair_limit", zdraw_pair_limit());
    zdraw_colorinfo_value(info, "path_pair_limit", zdraw_spans_pair_limit());
    zdraw_colorinfo_value(info, "max_pair", plan.max_pair);
    zdraw_colorinfo_value(info, "fits", plan.fits);
    zdraw_colorinfo_value(info, "request_limit", ZDRAW_RASTER_PLAN_CELLS);
    return !sethparam(args[0], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
}

static int
zdraw_raster_rgb_blit(const char *nam, struct zdraw_raster *s, WINDOW *win,
                      int row, int col, int half)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    struct zdraw_raster_plan plan;
    int result, x, y, index, height = (s->height+1)/2;
    unsigned int upper, lower;
    char colors[16];
    Colorpairnode pair;
    ZDrawCell *cells;
#ifdef ZDRAW_WIDE_SPANS
    wchar_t glyph[2];
#endif
    result = zdraw_raster_plan(&s, 1, half, &plan);
    if (result) return result;
    if (!plan.fits) return 1;
    cells = (ZDrawCell *)zhalloc((size_t)s->width*height*sizeof(*cells));
    /* Preflight catches budget failures before allocating. A terminal/library
     * allocation error may still retain pairs, but never changes cells. */
    for (y = 0; y < height; y++) {
        for (x = 0; x < s->width; x++) {
            index = 2*y*s->width+x;
            upper = s->rgb[index];
            lower = half && 2*y+1 < s->height ? s->rgb[index+s->width] : (unsigned int)s->ink[0];
            sprintf(colors, "#%06x/#%06x", upper, lower);
            pair = zdraw_colorget(nam, colors);
            if (!pair) return 1;
#ifdef ZDRAW_WIDE_SPANS
            glyph[0] = half ? (upper == lower ? L' ' : 0x2580) :
                (wchar_t)(unsigned char)s->glyph[s->material[index]];
            glyph[1] = 0;
            if (setcchar(cells+y*s->width+x, glyph, A_NORMAL, pair->colorpair, NULL) == ERR)
                return 1;
#else
            cells[y*s->width+x] = (unsigned char)s->glyph[s->material[index]] | COLOR_PAIR(pair->colorpair);
#endif
        }
    }
    for (y = 0; y < height; y++)
        if (zdraw_write_row(win, row+y, col, cells+y*s->width, s->width)) return 1;
    return 0;
#else
    (void)nam; (void)s; (void)win; (void)row; (void)col; (void)half;
    return 2;
#endif
}

static int
zdraw_raster_blit(const char *nam, struct zdraw_raster *s, char **args)
{
#if defined(ZDRAW_WIDE_SPANS) || defined(HAVE_WADDCHNSTR)
    LinkNode node;
    WINDOW *win;
    ZDrawCell *cells, *cache;
    unsigned char ready[ZDRAW_RASTER_PALETTE*ZDRAW_RASTER_PALETTE];
    int row, col, rows, cols, height = (s->height+1)/2;
    int half, mono, x, y, upper, lower, key, pair;
    Colorpairnode cpn;
    char colors[32];
#ifdef ZDRAW_WIDE_SPANS
    wchar_t glyph[2];
#endif
    half = !strcmp(args[3], "half");
    mono = !strcmp(args[3], "mono");
    if (!half && !mono && strcmp(args[3], "ascii")) return 1;
    if (!zdraw_getwindowbyname("stdscr")) return 1;
    node = zdraw_validate_window(args[0], ZDRAW_USED);
    if (!node || zdraw_nonnegative(args[1], &row) ||
        zdraw_nonnegative(args[2], &col)) return 1;
    win = ((ZCWin)getdata(node))->win;
    getmaxyx(win, rows, cols);
    if (row >= rows || col >= cols || height > rows-row || s->width > cols-col)
        return 1;
    if (half) {
#ifdef ZDRAW_WIDE_SPANS
        if (!isset(MULTIBYTE) || wcwidth(0x2580) != 1) return 2;
#else
        return 2;
#endif
    }
    if (s->rgb && !mono)
        return zdraw_raster_rgb_blit(nam, s, win, row, col, half);
    if (!mono) {
        if (!zc_color_started) return 2;
        for (x = 0; x < s->colors; x++)
            if (s->ink[x] >= COLORS) return 2;
    }
    cells = (ZDrawCell *)zhalloc((size_t)s->width*height*sizeof(*cells));
    cache = (ZDrawCell *)zhalloc(sizeof(ready)*sizeof(*cache));
    memset(ready, 0, sizeof(ready));
    /* Compile the entire rectangle before changing any window cell. Pair IDs
     * allocated before a later allocation failure remain session-owned, just
     * like spans; never recycle IDs retained by windows or prepared rows. */
    for (y = 0; y < height; y++) {
        for (x = 0; x < s->width; x++) {
            upper = s->material[2*y*s->width+x];
            lower = half && 2*y+1 < s->height ?
                s->material[(2*y+1)*s->width+x] : 0;
            key = upper*ZDRAW_RASTER_PALETTE + lower;
            if (!ready[key]) {
                pair = 0;
                if (!mono) {
                    if (!s->pairs[key]) {
                        sprintf(colors, "%d/%d", s->ink[upper], s->ink[lower]);
                        cpn = zdraw_colorget(nam, colors);
                        if (!cpn) return 1;
                        s->pairs[key] = cpn->colorpair;
                    }
                    pair = s->pairs[key];
                }
#ifdef ZDRAW_WIDE_SPANS
                glyph[0] = half ? (upper == lower ? L' ' : 0x2580) :
                    (wchar_t)(unsigned char)s->glyph[upper];
                glyph[1] = 0;
                if (setcchar(cache + key, glyph, A_NORMAL, (short)pair, NULL) == ERR)
                    return 1;
#else
                if (pair > PAIR_NUMBER(A_COLOR)) return 1;
                cache[key] = (unsigned char)s->glyph[upper] | COLOR_PAIR(pair);
#endif
                ready[key] = 1;
            }
            cells[y*s->width+x] = cache[key];
        }
    }
    for (y = 0; y < height; y++)
        if (zdraw_write_row(win, row+y, col, cells+y*s->width, s->width)) return 1;
    return 0;
#else
    (void)nam; (void)s; (void)args;
    return 2;
#endif
}

static int
zccmd_raster(const char *nam, char **args)
{
    struct zdraw_raster *s, **link;
    int nargs = arrlen(args), width, height, i, material, second;
    size_t bytes;
    unsigned int color;
    int rgb = !strcmp(args[0], "create-rgb");
    LinkList info;
    for (link = &zdraw_rasters; *link && strcmp((*link)->name, args[1]);
         link = &(*link)->next);
    s = *link;
    if (rgb || !strcmp(args[0], "create")) {
        if (nargs < 6 || (nargs-4)%2 || (nargs-4)/2 > ZDRAW_RASTER_PALETTE ||
            s || !isident(args[1]) || strchr(args[1], '[') ||
            strlen(args[1]) > 64 || zdraw_raster_size(args+2, &width, &height) ||
            zdraw_raster_count >= ZDRAW_RASTER_SURFACES) goto invalid;
        for (i = 4; i < nargs; i += 2)
            if ((rgb ? zdraw_raster_color(args[i], &color) :
                 (zdraw_nonnegative(args[i], &material) || material > 255)) ||
                strlen(args[i+1]) != 1 || args[i+1][0] < 32 || args[i+1][0] > 126)
                goto invalid;
        bytes = sizeof(*s) + strlen(args[1]) + 1;
        if (bytes > ZDRAW_RASTER_BYTES-zdraw_raster_bytes) goto invalid;
        s = (struct zdraw_raster *)zshcalloc(sizeof(*s));
        if (!s) return 1;
        s->rgb_mode = rgb;
        s->colors = (nargs-4)/2;
        for (i = 0; i < s->colors; i++) {
            if (rgb) {
                zdraw_raster_color(args[4+i*2], &color);
                s->ink[i] = (int)color;
            } else zdraw_nonnegative(args[4+i*2], &s->ink[i]);
            s->glyph[i] = args[5+i*2][0];
        }
        s->name = ztrdup(args[1]);
        s->bytes = bytes;
        zdraw_raster_bytes += bytes;
        zdraw_raster_count++;
        if (zdraw_raster_resize(s, width, height)) {
            zdraw_raster_free(s);
            goto invalid;
        }
        s->next = zdraw_rasters;
        zdraw_rasters = s;
        return 0;
    }
    if (!s) goto invalid;
    if (!strcmp(args[0], "free") && nargs == 2) {
        *link = s->next;
        zdraw_raster_free(s);
        return 0;
    }
    if (!strcmp(args[0], "resolve") && nargs == 5)
        return zdraw_raster_resolve(s, args+2);
    if (!strcmp(args[0], "plan") && nargs >= 3)
        return zdraw_raster_plan_command(nam, s, args+2, nargs-2);
    if (!strcmp(args[0], "resize") && nargs == 4) {
        if (zdraw_raster_size(args+2, &width, &height)) goto invalid;
        return zdraw_raster_resize(s, width, height);
    }
    if (!strcmp(args[0], "clear") && (nargs == 3 || nargs == 4)) {
        if (zdraw_nonnegative(args[2], &material) || material >= s->colors)
            goto invalid;
        second = material;
        if (nargs == 4 && (zdraw_nonnegative(args[3], &second) || second >= s->colors))
            goto invalid;
        for (i = 0; i < s->width*s->height; i++) {
            s->depth[i] = 0;
            s->material[i] = (unsigned short)(i < s->width*(s->height/2) ? material : second);
            if (s->rgb) s->rgb[i] = s->ink[s->material[i]];
        }
        return 0;
    }
    if (!strcmp(args[0], "triangles") || !strcmp(args[0], "triangles-rgb")) {
        if (zdraw_raster_batch(s, args+2, nargs-2, !strcmp(args[0], "triangles-rgb"))) goto invalid;
        return 0;
    }
    if (!strcmp(args[0], "rectangles")) {
        if (zdraw_raster_rectangles(s, args+2, nargs-2)) goto invalid;
        return 0;
    }
    if ((!strcmp(args[0], "read") || !strcmp(args[0], "read-rgb")) && nargs == 3)
        return zdraw_raster_read(nam, s, args[2], !strcmp(args[0], "read-rgb"));
    if (!strcmp(args[0], "blit") && nargs == 6)
        return zdraw_raster_blit(nam, s, args+2);
    if (!strcmp(args[0], "info") && nargs == 3) {
        if (zdraw_association(nam, args[2])) return 1;
        info = newlinklist();
        addlinknode(info, "format"); addlinknode(info, "zdraw-raster-experiment-1");
        zdraw_colorinfo_value(info, "rgb", s->rgb_mode);
        zdraw_colorinfo_value(info, "width", s->width);
        zdraw_colorinfo_value(info, "height", s->height);
        zdraw_colorinfo_value(info, "palette_size", s->colors);
        zdraw_colorinfo_value(info, "bytes", (zlong)s->bytes);
        zdraw_colorinfo_value(info, "total_bytes", (zlong)zdraw_raster_bytes);
        zdraw_colorinfo_value(info, "byte_limit", (zlong)ZDRAW_RASTER_BYTES);
        zdraw_colorinfo_value(info, "pixel_limit", ZDRAW_RASTER_PIXELS);
        zdraw_colorinfo_value(info, "dimension_limit", ZDRAW_RASTER_DIMENSION);
        zdraw_colorinfo_value(info, "triangle_limit", ZDRAW_RASTER_TRIANGLES);
        zdraw_colorinfo_value(info, "rectangle_limit", ZDRAW_RASTER_RECTANGLES);
        zdraw_colorinfo_value(info, "work_limit", ZDRAW_RASTER_WORK);
        return !sethparam(args[2], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
    }
invalid:
    zwarnnam(nam, "invalid raster operation, arguments, resource or budget");
    return 1;
}
