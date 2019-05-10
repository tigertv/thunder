#include "font.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/ftoutln.h>
#include <freetype/fttrigon.h>
#include <freetype/ftbitmap.h>

#include "log.h"

#define HEADER  "Header"
#define DATA    "Data"

struct Point {
    short dx, dy;
    int f;
};

struct Grid {
    int32_t w, h;
    Point *grid;
};

static Point pointInside = { 0, 0, 0 };
static Point pointEmpty = { 9999, 9999, 9999*9999 };

static FT_Library library = nullptr;
//FT_Done_FreeType(library);

static inline Point get(Grid &g, int32_t x, int32_t y) {
    return g.grid[y * (g.w + 2) + x];
}

static inline void put(Grid &g, int32_t x, int32_t y, const Point &p) {
    g.grid[y * (g.w + 2) + x] = p;
}

static inline void compare(Grid &g, Point &p, int32_t x, int32_t y, int32_t offsetx, int32_t offsety) {
    int add;
    Point other = get(g, x + offsetx, y + offsety);
    if(offsety == 0) {
        add = 2 * other.dx + 1;
    } else if(offsetx == 0) {
        add = 2 * other.dy + 1;
    } else {
        add = 2 * (other.dy + other.dx + 1);
    }
    other.f += add;
    if(other.f < p.f) {
        p.f = other.f;
        if(offsety == 0) {
            p.dx = other.dx + 1;
            p.dy = other.dy;
        } else if(offsetx == 0) {
            p.dy = other.dy + 1;
            p.dx = other.dx;
        } else {
            p.dy = other.dy + 1;
            p.dx = other.dx + 1;
        }
    }
}

static void generateSDF(Grid &g) {
    for(int32_t y = 1; y <= g.h; y++) {
        for(int32_t x = 1; x <= g.w; x++) {
            Point p = get(g, x, y);
            compare(g, p, x, y, -1,  0);
            compare(g, p, x, y,  0, -1);
            compare(g, p, x, y, -1, -1);
            compare(g, p, x, y,  1, -1);
            put(g, x, y, p);
        }
    }

    for(int32_t y = g.h; y > 0; y--) {
        for(int32_t x = g.w; x > 0; x--) {
            Point p = get(g, x, y);
            compare(g, p, x, y,  1,  0);
            compare(g, p, x, y,  0,  1);
            compare(g, p, x, y, -1,  1);
            compare(g, p, x, y,  1,  1);
            put(g, x, y, p);
        }
    }
}

void calculateDF(const FT_Bitmap &img, uint8_t *dst, double distanceFieldScale) {
    Grid grid[2];

    int32_t w = img.width;
    int32_t h = img.rows;
    grid[0].w = grid[1].w = w;
    grid[0].h = grid[1].h = h;
    grid[0].grid = static_cast<Point*>(malloc(sizeof(Point) * (w + 2) * (h + 2)));
    grid[1].grid = static_cast<Point*>(malloc(sizeof(Point) * (w + 2) * (h + 2)));
    /* create 1-pixel gap */
    for(int32_t x = 0; x < w + 2; x++) {
        put(grid[0], x, 0, pointInside);
        put(grid[1], x, 0, pointEmpty);
    }
    for(int32_t y = 1; y <= h; y++) {
        put(grid[0], 0, y, pointInside);
        put(grid[1], 0, y, pointEmpty);
        for(int32_t x = 1; x <= w; x++) {
            uint32_t index = (y - 1) * w + (x - 1);
            if(img.buffer[index] > 128) {
                put(grid[0], x, y, pointEmpty);
                put(grid[1], x, y, pointInside);
            } else {
                put(grid[0], x, y, pointInside);
                put(grid[1], x, y, pointEmpty);
            }
        }
        put(grid[0], w + 1, y, pointInside);
        put(grid[1], w + 1, y, pointEmpty);
    }
    for(int32_t x = 0; x < w + 2; x++) {
        put(grid[0], x, h + 1, pointInside);
        put(grid[1], x, h + 1, pointEmpty);
    }
    generateSDF(grid[0]);
    generateSDF(grid[1]);



    for(int32_t y = 1; y <= h; y++) {
        for(int32_t x = 1; x <= w; x++) {
            double dist1 = sqrt((double)(get(grid[0], x, y).f + 1));
            double dist2 = sqrt((double)(get(grid[1], x, y).f + 1));
            double dist = dist1 - dist2;
            uint32_t index = (y - 1) * w + (x - 1);
            dst[index] = CLAMP(dist * 64 / distanceFieldScale + 128, 0, 255);
        }
    }
    free(grid[0].grid);
    free(grid[1].grid);
}

Font::Font() :
        Atlas(),
        m_Size(12),
        m_pFace(nullptr),
        m_UseKerning(false) {

    clear();

    if(FT_Init_FreeType( &library )) {
        // FT_Init_FreeType failed
    }
}

Font::~Font() {
     clear();
}

uint32_t Font::size() const {
    return m_Size;
}

uint32_t Font::atlasIndex(uint32_t glyph, uint32_t size) const {
    if(size == 0) {
        size    = m_Size;
    }
    uint32_t ch = glyph ^ size;
    auto it = m_GlyphMap.find(ch);
    if(it != m_GlyphMap.end()) {
        return (*it).second;
    }
    return 0;
}

void Font::requestCharacters(const u32string &characters, uint32_t size) {
    if(size == 0) {
        size    = m_Size;
    }

    FT_Error error  = FT_Set_Char_Size( m_pFace, size * 64, 0, 100, 0 );
    if(!error) {
        for(auto it : characters) {
            uint32_t ch = it ^ size;
            if(m_GlyphMap.find(ch) == m_GlyphMap.end() && m_pFace) {
                error   = FT_Load_Glyph( m_pFace, FT_Get_Char_Index( m_pFace, it ), FT_LOAD_DEFAULT );
                if(!error) {
                    FT_Glyph glyph;
                    error   = FT_Get_Glyph( m_pFace->glyph, &glyph );
                    if(!error) {
                        FT_Glyph_To_Bitmap( &glyph, ft_render_mode_normal, nullptr, 1 );
                        FT_Bitmap &bitmap   = reinterpret_cast<FT_BitmapGlyph>(glyph)->bitmap;

                        uint32_t size   = bitmap.width * bitmap.rows;

                        Texture::Surface s;
                        uint8_t *buffer = new uint8_t[size];
                        calculateDF(bitmap, buffer, 1);

                        s.push_back(buffer);

                        FT_BBox bbox;
                        FT_Glyph_Get_CBox(glyph, ft_glyph_bbox_pixels, &bbox);

                        Texture *t  = Engine::objectCreate<Texture>("", this);
                        t->setWidth(bitmap.width);
                        t->setHeight(bitmap.rows);

                        Vector2Vector shape;
                        shape.resize(4);
                        shape[0] = Vector2(bbox.xMin, bbox.yMax);
                        shape[1] = Vector2(bbox.xMax, bbox.yMax);
                        shape[2] = Vector2(bbox.xMax, bbox.yMin);
                        shape[3] = Vector2(bbox.xMin, bbox.yMin);

                        t->setShape(shape);
                        t->addSurface(s);

                        m_GlyphMap[ch] = addElement(t);
                    }
                }
            }
        }
        pack(1);
        m_pTexture->apply();
    }
}

int32_t Font::requestKerning(uint32_t glyph, uint32_t previous) const {
    if(m_UseKerning && previous)  {
        FT_Vector delta;
        FT_Get_Kerning( m_pFace, previous, glyph, FT_KERNING_DEFAULT, &delta );
        return delta.x >> 6;
    }
    return 0;
}

void Font::setFontName(const string &name) {
    clear();

    m_FontName  = name;

    FT_Error error  = FT_New_Face( library, m_FontName.c_str(), 0, &m_pFace );
    if(error) {
        Log(Log::ERR) << "Can't load font" << name.c_str() << "system returned error:" << error;
        return;
    }

    m_UseKerning = FT_HAS_KERNING( m_pFace );
}

uint32_t Font::length(const u32string &characters) const {
    return characters.length();
}

uint32_t Font::spaceWidth(uint32_t size) const {
    if(size == 0) {
        size    = m_Size;
    }

    FT_Error error  = FT_Set_Char_Size( m_pFace, size * 64, 0, 100, 0 );
    if(!error) {
        error   = FT_Load_Glyph( m_pFace, FT_Get_Char_Index( m_pFace, ' ' ), FT_LOAD_DEFAULT );
        if(!error) {
            return m_pFace->glyph->advance.x / 64;
        }
    }
    return 0;
}

uint32_t Font::lineHeight(uint32_t size) const {
    if(size == 0) {
        size    = m_Size;
    }

    FT_Error error  = FT_Set_Char_Size( m_pFace, size * 64, 0, 100, 0 );
    if(!error) {
        error   = FT_Load_Glyph( m_pFace, FT_Get_Char_Index( m_pFace, '\n' ), FT_LOAD_DEFAULT );
        if(!error) {
            return m_pFace->glyph->metrics.height / 32;
        }
    }
    return 0;
}

void Font::loadUserData(const VariantMap &data) {
    clear();
    {
        auto it = data.find(HEADER);
        if(it != data.end()) {
            VariantList header  = (*it).second.value<VariantList>();

            auto i      = header.begin();
            //Reserved
            i++;
            m_Size      = (*i).toInt();
            i++;
            string name = (*i).toString();
            i++;
            if(!name.empty()) {
                setFontName(name);
            }
        }
    }

    {
        auto it = data.find(DATA);
        if(it != data.end()) {
            m_Data  = (*it).second.toByteArray();
            FT_Error error = FT_New_Memory_Face(library, reinterpret_cast<const uint8_t *>(&m_Data[0]), m_Data.size(), 0, &m_pFace);
            if(error) {
                Log(Log::ERR) << "Can't load font. System returned error:" << error;
                return;
            }
            m_UseKerning = FT_HAS_KERNING( m_pFace );
        }
    }
}

void Font::clear() {
    FT_Done_Face(m_pFace);
}
