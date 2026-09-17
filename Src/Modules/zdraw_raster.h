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
#define ZDRAW_RASTER_WORK 16777216

struct zdraw_raster {
    struct zdraw_raster *next;
    char *name;
    int width, height, colors;
    int ink[ZDRAW_RASTER_PALETTE];
    char glyph[ZDRAW_RASTER_PALETTE];
    short pairs[ZDRAW_RASTER_PALETTE * ZDRAW_RASTER_PALETTE];
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
    size_t pixels = (size_t)width * height;
    size_t bytes = pixels * (sizeof(*s->depth) + sizeof(*s->material));
    size_t old = (size_t)s->width * s->height *
        (sizeof(*s->depth) + sizeof(*s->material));
    double *depth;
    unsigned short *material;
    if (bytes > ZDRAW_RASTER_BYTES - zdraw_raster_bytes) return 1;
    depth = (double *)zshcalloc(pixels * sizeof(*depth));
    if (!depth) return 1;
    material = (unsigned short *)zshcalloc(pixels * sizeof(*material));
    if (!material) {
        zfree(depth, pixels * sizeof(*depth));
        return 1;
    }
    zfree(s->depth, (size_t)s->width * s->height * sizeof(*depth));
    zfree(s->material, (size_t)s->width * s->height * sizeof(*material));
    s->depth = depth;
    s->material = material;
    s->width = width;
    s->height = height;
    s->bytes = s->bytes - old + bytes;
    zdraw_raster_bytes = zdraw_raster_bytes - old + bytes;
    return 0;
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
zdraw_raster_triangle(struct zdraw_raster *s, struct zdraw_triangle *t)
{
    double da, db, dq, a, b, q, px, py;
    int x, y, index;
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
            }
            a += da;
            b += db;
            q += dq;
        }
    }
}

static int
zdraw_raster_batch(struct zdraw_raster *s, char **args, int nargs)
{
    struct zdraw_triangle *batch, *t;
    size_t work = 0, area;
    int count, i, j;
    if (nargs % 10 || nargs / 10 > ZDRAW_RASTER_TRIANGLES) return 1;
    count = nargs / 10;
    batch = (struct zdraw_triangle *)zhalloc((size_t)count * sizeof(*batch));
    /* All validation precedes writes, including the total traversal budget. */
    for (i = 0; i < count; i++, args += 10) {
        t = batch + i;
        for (j = 0; j < 3; j++) {
            if (zdraw_raster_number(args[j*3], &t->x[j], 32768) ||
                zdraw_raster_number(args[j*3+1], &t->y[j], 32768) ||
                zdraw_raster_number(args[j*3+2], &t->q[j], 1000000) ||
                t->q[j] <= 0) return 1;
        }
        if (zdraw_nonnegative(args[9], &t->material) ||
            t->material >= s->colors) return 1;
        zdraw_raster_bounds(s, t);
        if (t->left <= t->right && t->top <= t->bottom) {
            area = (size_t)(t->right-t->left+1)*(t->bottom-t->top+1);
            if (area > ZDRAW_RASTER_WORK - work) return 1;
            work += area;
        }
    }
    for (i = 0; i < count; i++) zdraw_raster_triangle(s, batch + i);
    return 0;
}

static int
zdraw_raster_read(const char *nam, struct zdraw_raster *s, char *target)
{
    Param pm;
    char **data, number[64];
    int i, pixels = s->width*s->height;
    if (!isident(target) || strchr(target, '[')) return 1;
    pm = (Param)gethashnode2(paramtab, target);
    if (pm && ((pm->node.flags & (PM_READONLY|PM_SPECIAL)) ||
               PM_TYPE(pm->node.flags) != PM_ARRAY)) {
        zwarnnam(nam, "raster read expects an ordinary writable array");
        return 1;
    }
    data = (char **)zalloc((size_t)(pixels*2+1)*sizeof(*data));
    for (i = 0; i < pixels; i++) {
        sprintf(number, "%u", (unsigned int)s->material[i]);
        data[i*2] = ztrdup(number);
        sprintf(number, "%.17g", s->depth[i]);
        data[i*2+1] = ztrdup(number);
    }
    data[pixels*2] = NULL;
    return !setaparam(target, data) || (errflag & ERRFLAG_ERROR);
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
    LinkList info;
    for (link = &zdraw_rasters; *link && strcmp((*link)->name, args[1]);
         link = &(*link)->next);
    s = *link;
    if (!strcmp(args[0], "create")) {
        if (nargs < 6 || (nargs-4)%2 || (nargs-4)/2 > ZDRAW_RASTER_PALETTE ||
            s || !isident(args[1]) || strchr(args[1], '[') ||
            strlen(args[1]) > 64 || zdraw_raster_size(args+2, &width, &height) ||
            zdraw_raster_count >= ZDRAW_RASTER_SURFACES) goto invalid;
        for (i = 4; i < nargs; i += 2)
            if (zdraw_nonnegative(args[i], &material) || material > 255 ||
                strlen(args[i+1]) != 1 || args[i+1][0] < 32 || args[i+1][0] > 126)
                goto invalid;
        bytes = sizeof(*s) + strlen(args[1]) + 1;
        if (bytes > ZDRAW_RASTER_BYTES-zdraw_raster_bytes) goto invalid;
        s = (struct zdraw_raster *)zshcalloc(sizeof(*s));
        if (!s) return 1;
        s->name = ztrdup(args[1]);
        s->bytes = bytes;
        zdraw_raster_bytes += bytes;
        zdraw_raster_count++;
        if (zdraw_raster_resize(s, width, height)) {
            zdraw_raster_free(s);
            goto invalid;
        }
        s->colors = (nargs-4)/2;
        for (i = 0; i < s->colors; i++) {
            zdraw_nonnegative(args[4+i*2], &s->ink[i]);
            s->glyph[i] = args[5+i*2][0];
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
        }
        return 0;
    }
    if (!strcmp(args[0], "triangles")) {
        if (zdraw_raster_batch(s, args+2, nargs-2)) goto invalid;
        return 0;
    }
    if (!strcmp(args[0], "read") && nargs == 3)
        return zdraw_raster_read(nam, s, args[2]);
    if (!strcmp(args[0], "blit") && nargs == 6)
        return zdraw_raster_blit(nam, s, args+2);
    if (!strcmp(args[0], "info") && nargs == 3) {
        if (zdraw_association(nam, args[2])) return 1;
        info = newlinklist();
        addlinknode(info, "format"); addlinknode(info, "zdraw-raster-experiment-1");
        zdraw_colorinfo_value(info, "width", s->width);
        zdraw_colorinfo_value(info, "height", s->height);
        zdraw_colorinfo_value(info, "palette_size", s->colors);
        zdraw_colorinfo_value(info, "bytes", (zlong)s->bytes);
        zdraw_colorinfo_value(info, "total_bytes", (zlong)zdraw_raster_bytes);
        zdraw_colorinfo_value(info, "byte_limit", (zlong)ZDRAW_RASTER_BYTES);
        zdraw_colorinfo_value(info, "pixel_limit", ZDRAW_RASTER_PIXELS);
        zdraw_colorinfo_value(info, "dimension_limit", ZDRAW_RASTER_DIMENSION);
        zdraw_colorinfo_value(info, "triangle_limit", ZDRAW_RASTER_TRIANGLES);
        zdraw_colorinfo_value(info, "work_limit", ZDRAW_RASTER_WORK);
        return !sethparam(args[2], zdraw_list_array(info)) || (errflag & ERRFLAG_ERROR);
    }
invalid:
    zwarnnam(nam, "invalid raster operation, arguments, resource or budget");
    return 1;
}
