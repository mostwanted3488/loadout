#include "WheelOverlay.h"

using namespace juce::gl;

//==============================================================================
namespace
{
    constexpr float RO = 246.0f, RI = 104.0f, LR = 172.0f;

    std::unique_ptr<WheelOverlay>& currentOverlay()   { static std::unique_ptr<WheelOverlay> o; return o; }
    bool& captureInFlight()                           { static bool b = false; return b; }

    juce::Point<float> dirOf (int i)
    {
        const auto a = (float) i * juce::MathConstants<float>::pi / 5.0f;
        return { std::sin (a), -std::cos (a) };
    }

    juce::Font font (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions().withHeight (h).withStyle (bold ? "Bold" : "Regular"));
    }

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    juce::String fit (const juce::String& s, int maxChars = 15)
    {
        return s.length() > maxChars ? s.substring (0, maxChars - 1) + U ("\xe2\x80\xa6") : s;
    }

    void shadowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                     juce::Justification j, juce::Colour c)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f * c.getFloatAlpha()));
        g.drawText (text, r.translated (0.0f, 1.0f), j, false);
        g.setColour (c);
        g.drawText (text, r, j, false);
    }

    juce::Path magnifier (juce::Point<float> c, float size)
    {
        juce::Path p;
        const float r = size * 0.27f;
        const auto cc = c + juce::Point<float> (-size * 0.07f, -size * 0.07f);
        p.addEllipse (cc.x - r, cc.y - r, r * 2, r * 2);
        p.startNewSubPath (cc + juce::Point<float> (r * 0.72f, r * 0.72f));
        p.lineTo (c + juce::Point<float> (size * 0.42f, size * 0.42f));
        return p;
    }

    juce::Colour colourFor (const juce::String& name)
    {
        const auto h = (float) (std::abs (name.hashCode()) % 360) / 360.0f;
        return juce::Colour::fromHSV (h, 0.55f, 0.85f, 1.0f);
    }

    juce::Image makeFallbackBackdrop()
    {
        juce::Image img (juce::Image::ARGB, 960, 540, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff06080d));
        struct Blob { juce::Colour c; float x, y; };
        for (auto b : { Blob { juce::Colour (0xff2b6cff), .55f, .35f }, Blob { juce::Colour (0xff00d4ff), .7f, .6f },
                        Blob { juce::Colour (0xff8a2be2), .3f, .7f },   Blob { juce::Colour (0xffff3d7f), .65f, .25f } })
        {
            const juce::Point<float> c (960.0f * b.x, 540.0f * b.y);
            g.setGradientFill (juce::ColourGradient (b.c.withAlpha (0.75f), c, b.c.withAlpha (0.0f), c + juce::Point<float> (320.0f, 0.0f), true));
            g.fillRect (img.getBounds());
        }
        return img;
    }

    const char* vertexShader = R"(#version 150
in vec2 a;
void main() { gl_Position = vec4 (a, 0.0, 1.0); }
)";

    const char* fragmentShader = R"(#version 150
uniform sampler2D uTex;
uniform vec2 uRes;
uniform float uDpr;
uniform vec2 uC;
uniform float uS;
uniform float uH[10];
uniform float uE[10];
uniform float uM;
uniform float uOpen;
uniform float uPress;
uniform vec2 uMouse;
uniform vec4 uR[24];
uniform vec4 uRP[24];
uniform int uRN;
out vec4 fragColour;

const float RO = 246.0, RI = 104.0, RM = 175.0, HW = 71.0, G = 3.5, CR = 16.0;
const float TAU = 6.2831853, SEG = 0.62831853;

float segD (vec2 q, int k)
{
    float a = float (k) * SEG, c = cos (a), s = sin (a);
    vec2 l = vec2 (c * q.x + s * q.y, -s * q.x + c * q.y);
    float h = uH[k];
    vec2 u = vec2 (abs (l.x), -l.y - h * 9.0);
    float da = abs (length (u) - RM) - HW;
    float dp = dot (u, vec2 (0.95105652, -0.30901699)) + G;
    float A = da + CR, B = dp + CR;
    return length (max (vec2 (A, B), 0.0)) + min (max (A, B), 0.0) - CR - h * 2.5;
}

float boxD (vec2 q)
{
    vec2 hs = mix (vec2 (84.0), vec2 (190.0, 28.0), uM);
    hs *= vec2 (1.0 + 0.06 * uPress, 1.0 - 0.08 * uPress);
    hs = max (hs, vec2 (20.0));
    float r = min (hs.x, hs.y);
    vec2 d = abs (q) - hs + r;
    return length (max (d, 0.0)) + min (max (d.x, d.y), 0.0) - r;
}

float ringD (vec2 q, out float hv, out float em)
{
    float th = atan (q.x, -q.y);
    if (th < 0.0) th += TAU;
    int k0 = int (floor (th / SEG + 0.5)) % 10;
    float dr = 1e5; hv = 0.0; em = 0.0;
    for (int j = -1; j <= 1; j++)
    {
        int k = (k0 + j + 10) % 10;
        float d = segD (q, k);
        if (d < dr) { dr = d; hv = uH[k]; em = uE[k]; }
    }
    return dr;
}

float rd (vec2 q) { float a, b; return ringD (q, a, b); }

float rbox (vec2 p, vec4 r, float rad)
{
    vec2 h = r.zw * 0.5;
    vec2 d = abs (p - (r.xy + h)) - h + rad;
    return length (max (d, 0.0)) + min (max (d.x, d.y), 0.0) - rad;
}

vec3 blurS (vec2 uv)
{
    vec3 s = vec3 (0.0);
    vec2 px = 1.0 / uRes;
    for (int i = 0; i < 8; i++)
    {
        float a = float (i) * 2.39996 + 0.5, r = sqrt (float (i) + 0.5) / 2.83;
        s += textureLod (uTex, uv + vec2 (cos (a), sin (a)) * r * 11.0 * px, 2.2).rgb;
    }
    return s / 8.0;
}

vec3 shade (vec2 p, vec2 uv, float dpx, vec2 n, float tint, float em, float bevel, float refr, float dark)
{
    float t = clamp (-dpx / bevel, 0.0, 1.0), edge = 1.0 - t, bend = pow (edge, 2.3);
    vec2 o2 = (n * bend * refr) / uRes;
    vec3 g = vec3 (blurS (uv + o2 * 1.32).r, blurS (uv + o2).g, blurS (uv + o2 * 0.68).b);
    g = g * 0.86 + 0.04;
    g = mix (g, vec3 (1.0), tint);
    g *= 1.0 - 0.45 * em;
    g *= 1.0 - dark;
    vec2 L1 = normalize (vec2 (-0.55, -1.0));
    float rim = pow (edge, 3.2);
    float spec = rim * (0.65 * pow (max (dot (n, L1), 0.0), 2.0) + 0.3 * pow (max (dot (n, -L1), 0.0), 3.0));
    vec2 md = uMouse - p; float ml = length (md);
    spec += rim * 0.9 * pow (max (dot (n, md / max (ml, 1.0)), 0.0), 7.0) * exp (-ml / 240.0);
    spec += pow (edge, 8.0) * 0.12;
    float line = exp (-abs (dpx + 0.7) * 1.5) * 0.25;
    g += vec3 (spec + line);
    g += vec3 (0.04) * (1.0 - smoothstep (0.0, 0.6, t)) * max (dot (n, vec2 (0.0, -1.0)), 0.0);
    return g;
}

void main()
{
    vec2 p = vec2 (gl_FragCoord.x, uRes.y * uDpr - gl_FragCoord.y) / uDpr;
    vec2 uv = p / uRes;
    vec3 col = texture (uTex, uv).rgb;
    float sc = uS * (0.86 + 0.14 * uOpen);
    vec2 q = (p - uC) / sc;
    vec2 e = vec2 (0.8, 0.0);

    if (uOpen > 0.002 && length (q) < 340.0)
    {
        float hv, em;
        float d = ringD (q, hv, em);
        col *= 1.0 - (1.0 - smoothstep (-24.0, 50.0, rd (q - vec2 (0.0, 18.0)))) * 0.45 * uOpen;
        if (d < 2.0)
        {
            vec2 n = normalize (vec2 (rd (q + e.xy) - rd (q - e.xy), rd (q + e.yx) - rd (q - e.yx)) + 1e-6);
            vec3 g = shade (p, uv, d * sc, n, 0.05 + hv * 0.13, em, 26.0 * sc, 38.0 * sc, 0.0);
            col = mix (col, g, clamp (0.5 - d * sc * uDpr, 0.0, 1.0) * uOpen);
        }

        float dc = boxD (q);
        col *= 1.0 - (1.0 - smoothstep (-16.0, 34.0, boxD (q - vec2 (0.0, 12.0)))) * 0.35 * uOpen * smoothstep (0.0, 0.4, uM);
        if (dc < 2.0)
        {
            vec2 n = normalize (vec2 (boxD (q + e.xy) - boxD (q - e.xy), boxD (q + e.yx) - boxD (q - e.yx)) + 1e-6);
            vec3 g = shade (p, uv, dc * sc, n, 0.08, 0.0, mix (30.0, 20.0, clamp (uM, 0.0, 1.0)) * sc, 38.0 * sc, 0.0);
            col = mix (col, g, clamp (0.5 - dc * sc * uDpr, 0.0, 1.0) * uOpen);
        }
    }

    for (int i = 0; i < 24; i++)
    {
        if (i >= uRN) break;
        vec4 r = uR[i];
        vec4 rp = uRP[i];
        float rad = min (rp.x, min (r.z, r.w) * 0.5);
        col *= 1.0 - (1.0 - smoothstep (-8.0, 22.0, rbox (p - vec2 (0.0, 6.0), r, rad))) * 0.25 * rp.y;
        float d = rbox (p, r, rad);
        if (d < 1.5)
        {
            vec2 ee = vec2 (0.6, 0.0);
            vec2 n = normalize (vec2 (rbox (p + ee.xy, r, rad) - rbox (p - ee.xy, r, rad),
                                      rbox (p + ee.yx, r, rad) - rbox (p - ee.yx, r, rad)) + 1e-6);
            vec3 g = shade (p, uv, d, n, 0.07 + rp.w, 0.0, min (14.0, rad), 14.0, rp.z);
            col = mix (col, g, clamp (0.5 - d * uDpr, 0.0, 1.0) * rp.y);
        }
    }

    fragColour = vec4 (col, 1.0);
}
)";
}

//==============================================================================
void WheelOverlay::open (LoadoutProcessor& owner)
{
    if (currentOverlay() != nullptr)
    {
        currentOverlay()->toFront (true);
        currentOverlay()->grabKeyboardFocus();
        return;
    }

    if (captureInFlight())
        return;

    auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForPoint (juce::Desktop::getMousePosition());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display == nullptr)
        return;

    const auto area = display->totalArea;
    captureInFlight() = true;

    Platform::captureScreen (area, [o = &owner, alive = owner.aliveFlag(), area] (juce::Image img)
    {
        captureInFlight() = false;
        if (*alive && currentOverlay() == nullptr)
            currentOverlay().reset (new WheelOverlay (*o, img, area));
    });
}

void WheelOverlay::closeIfOwnedBy (LoadoutProcessor& p)
{
    if (currentOverlay() != nullptr && &currentOverlay()->owner == &p)
        currentOverlay().reset();
}

bool WheelOverlay::isOpen()   { return currentOverlay() != nullptr; }

//==============================================================================
WheelOverlay::WheelOverlay (LoadoutProcessor& o, juce::Image img, juce::Rectangle<int> area)
    : owner (o), ownerAlive (o.aliveFlag())
{
    snapshot = img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : makeFallbackBackdrop();
    recents = lib->getRecents();

    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (true);
    gl.attachTo (*this);

    setBounds (area);
    addToDesktop (0);
    setAlwaysOnTop (true);
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();

    sound (UISounds::open);
    layout();
    startTimerHz (60);
}

WheelOverlay::~WheelOverlay()
{
    stopTimer();
    gl.detach();
}

//==============================================================================
void WheelOverlay::newOpenGLContextCreated()
{
    shader = std::make_unique<juce::OpenGLShaderProgram> (gl);
    if (! shader->addVertexShader (vertexShader) || ! shader->addFragmentShader (fragmentShader) || ! shader->link())
    {
        DBG ("LOADOUT shader error: " << shader->getLastError());
        shader.reset();
        return;
    }

    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    const GLfloat tri[] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    glBufferData (GL_ARRAY_BUFFER, sizeof (tri), tri, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    glGenTextures (1, &tex);
    glBindTexture (GL_TEXTURE_2D, tex);
    {
        juce::Image::BitmapData bd (snapshot, juce::Image::BitmapData::readOnly);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei (GL_UNPACK_ROW_LENGTH, bd.lineStride / bd.pixelStride);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, bd.width, bd.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, bd.data);
        glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap (GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, 0);
}

void WheelOverlay::openGLContextClosing()
{
    if (tex != 0) glDeleteTextures (1, &tex);
    if (vbo != 0) glDeleteBuffers (1, &vbo);
    if (vao != 0) glDeleteVertexArrays (1, &vao);
    tex = vbo = vao = 0;
    shader.reset();
}

void WheelOverlay::renderOpenGL()
{
    juce::OpenGLHelpers::clear (juce::Colours::black);
    if (shader == nullptr)
        return;

    Uniforms u;
    {
        const juce::SpinLock::ScopedLockType sl (uniformLock);
        u = uniforms;
    }

    shader->use();
    const auto prog = shader->getProgramID();
    auto loc = [prog] (const char* name) { return glGetUniformLocation (prog, name); };

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, tex);
    glUniform1i (loc ("uTex"), 0);
    glUniform2f (loc ("uRes"), u.w, u.h);
    glUniform1f (loc ("uDpr"), (GLfloat) gl.getRenderingScale());
    glUniform2f (loc ("uC"), u.cx, u.cy);
    glUniform1f (loc ("uS"), u.scale);
    glUniform1fv (loc ("uH"), N, u.H);
    glUniform1fv (loc ("uE"), N, u.E);
    glUniform1f (loc ("uM"), u.M);
    glUniform1f (loc ("uOpen"), u.openA);
    glUniform1f (loc ("uPress"), u.press);
    glUniform2f (loc ("uMouse"), u.mx, u.my);
    glUniform4fv (loc ("uR"), maxGlass, u.rects);
    glUniform4fv (loc ("uRP"), maxGlass, u.rectParams);
    glUniform1i (loc ("uRN"), u.numGlass);

    glDisable (GL_BLEND);
    glBindVertexArray (vao);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    const auto attr = (GLuint) glGetAttribLocation (prog, "a");
    glEnableVertexAttribArray (attr);
    glVertexAttribPointer (attr, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray (attr);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindTexture (GL_TEXTURE_2D, 0);
    glUseProgram (0);
}

//==============================================================================
float WheelOverlay::wheelScale() const
{
    return juce::jmin (1.0f, ((float) getHeight() - 130.0f) / 520.0f, ((float) getWidth() - 24.0f) / 520.0f);
}

juce::Point<float> WheelOverlay::toDesign (juce::Point<float> p) const
{
    return (p - centre()) / animScale();
}

juce::Point<float> WheelOverlay::centreHalfSize() const
{
    return { 84.0f + (190.0f - 84.0f) * M, 84.0f + (28.0f - 84.0f) * M };
}

bool WheelOverlay::inCentre (juce::Point<float> q) const
{
    const auto hs = centreHalfSize();
    const float r = juce::jmin (hs.x, hs.y);
    const float dx = juce::jmax (std::abs (q.x) - hs.x + r, 0.0f);
    const float dy = juce::jmax (std::abs (q.y) - hs.y + r, 0.0f);
    return std::hypot (dx, dy) < r + 2.0f;
}

int WheelOverlay::segmentAt (juce::Point<float> q) const
{
    float a = std::atan2 (q.x, -q.y);
    if (a < 0) a += juce::MathConstants<float>::twoPi;
    return ((int) std::floor (a / (juce::MathConstants<float>::pi / 5.0f) + 0.5f)) % N;
}

juce::Array<PluginLibrary::Item> WheelOverlay::currentItems() const
{
    juce::Array<Item> out;
    auto slice = [&] (const juce::Array<Item>& all)
    {
        for (int i = page * N; i < juce::jmin (all.size(), (page + 1) * N); ++i)
            out.add (all.getReference (i));
    };

    if (dragging || (! searching && ! inFolder))
        for (int i = 0; i < N; ++i)
            out.add (lib->getSlot (i));
    else if (searching)
        slice (searchResults);
    else
        slice (folderItems);
    return out;
}

int WheelOverlay::numPages() const
{
    if (dragging || (! searching && ! inFolder))
        return 1;
    const int n = searching ? searchResults.size() : folderItems.size();
    return juce::jmax (1, (n + N - 1) / N);
}

void WheelOverlay::setHover (int i)
{
    if (i != hover)
    {
        hover = i;
        if (i >= 0) sound (UISounds::tick);
        needsRepaint = true;
    }
}

void WheelOverlay::contentChanged()
{
    labelA = 0.0f;
    needsRepaint = true;
}

void WheelOverlay::sound (UISounds::Sound s)
{
    if (lib->soundEnabled)
        UISounds::play (s);
}

void WheelOverlay::keySound (juce::juce_wchar ch, bool backspace)
{
    if (lib->soundEnabled)
        UISounds::playKey (ch, backspace);
}

void WheelOverlay::toast (const juce::String& label, const juce::String& text)
{
    toastLabel = label;
    toastText = text;
    toastUntil = juce::Time::getMillisecondCounterHiRes() + 1700.0;
    needsRepaint = true;
}

//==============================================================================
void WheelOverlay::activate (int i)
{
    const auto items = currentItems();
    if (! juce::isPositiveAndBelow (i, items.size()))
        return;

    const auto it = items[i];
    if (it.isEmpty())
        return;

    HV[i] -= 0.12f;
    if (it.isFolder()) enterFolder (it);
    else               choose (it);
}

void WheelOverlay::enterFolder (const Item& it)
{
    inFolder = true;
    folderName = it.name;
    folderItems = lib->getFolderContents (it.name);
    page = 0;
    hover = -1;
    contentChanged();
    sound (UISounds::folder);
    if (folderItems.isEmpty())
        toast ("Empty", it.name + " has no plugins yet");
}

void WheelOverlay::back()
{
    if (! inFolder)
        return;
    inFolder = false;
    page = 0;
    hover = -1;
    contentChanged();
    sound (UISounds::back);
}

void WheelOverlay::openSearch()
{
    if (searching)
        return;
    searching = true;
    press = 1.0f;
    query.clear();
    searchResults.clear();
    page = 0;
    hover = -1;
    contentChanged();
    sound (UISounds::search);
}

void WheelOverlay::closeSearch (bool silent)
{
    searching = false;
    press = 1.0f;
    query.clear();
    searchResults.clear();
    page = 0;
    hover = -1;
    contentChanged();
    if (! silent) sound (UISounds::close);
}

void WheelOverlay::runSearch()
{
    searchResults = lib->search (query, 100);
    page = 0;
    hover = searchResults.isEmpty() ? -1 : 0;
    needsRepaint = true;
}

void WheelOverlay::choose (const Item& it)
{
    if (! it.isPlugin())
        return;

    if (searching)
        closeSearch (true);

    if (lib->pickTracks)
        openPicker (it);
    else
        commit (it, { Target { &owner, ownerAlive, owner.getDisplayName(), true } });
}

void WheelOverlay::commit (const Item& it, const std::vector<Target>& to)
{
    auto desc = lib->findById (it.id);
    if (desc == nullptr)
    {
        toast ("Not found", it.name + " - rescan your plugins");
        sound (UISounds::clear);
        return;
    }

    int loaded = 0;
    juce::String lastError;
    for (auto& t : to)
    {
        if (! *t.alive)
            continue;
        juce::String err;
        if (t.proc->loadPlugin (*desc, err)) ++loaded;
        else lastError = err;
    }

    if (loaded == 0)
    {
        toast ("Error", lastError.isNotEmpty() ? lastError : juce::String ("could not load ") + it.name);
        sound (UISounds::clear);
        return;
    }

    lib->pushRecent (it);
    sound (UISounds::select);
    openPluginAfterClose = (to.size() == 1 && to.front().isOwner);
    closing = true;
}

void WheelOverlay::startDrag (const Item& it)
{
    dragging = true;
    dragItem = it;
    pending.active = false;
    hover = -1;
    contentChanged();
    sound (UISounds::toggle);
}

void WheelOverlay::endDrag (bool drop)
{
    if (! dragging)
        return;

    if (drop && hover >= 0)
    {
        const auto prev = lib->getSlot (hover);
        lib->setSlot (hover, dragItem);
        sound (UISounds::pin);
        toast ("Pinned", dragItem.name + U ("  \xe2\x86\x92  slot ") + juce::String ((hover + 1) % 10)
                           + ((prev.isEmpty() || prev.name == dragItem.name) ? juce::String() : "  (replaces " + prev.name + ")"));
        HV[hover] -= 0.25f;
    }

    dragging = false;
    if (searching)
        closeSearch (true);
    hover = -1;
    contentChanged();
}

void WheelOverlay::clearSlot (int i)
{
    const auto prev = lib->getSlot (i);
    if (prev.isEmpty())
        return;
    lib->setSlot (i, Item());
    sound (UISounds::clear);
    toast ("Cleared", "slot " + juce::String ((i + 1) % 10) + " (" + prev.name + ")");
    contentChanged();
}

void WheelOverlay::openPicker (const Item& it)
{
    pickerOpen = true;
    pickerItem = it;
    targets.clear();
    picked.clear();

    for (auto* p : LoadoutProcessor::getAllInstances())
        targets.push_back ({ p, p->aliveFlag(), p->getDisplayName(), p == &owner });

    std::stable_sort (targets.begin(), targets.end(), [] (const Target& a, const Target& b) { return a.isOwner && ! b.isOwner; });
    if (targets.size() > 20)
        targets.resize (20);

    picked.insert (0);
    sound (UISounds::open);
    needsRepaint = true;
}

void WheelOverlay::closePicker()
{
    pickerOpen = false;
    sound (UISounds::close);
    needsRepaint = true;
}

void WheelOverlay::togglePick (int k)
{
    if (! juce::isPositiveAndBelow (k, (int) targets.size()))
        return;
    if (picked.count (k)) picked.erase (k);
    else                  picked.insert (k);
    sound (UISounds::toggle);
    needsRepaint = true;
}

void WheelOverlay::confirmPicker()
{
    if (picked.empty())
        return;
    std::vector<Target> sel;
    for (auto k : picked)
        sel.push_back (targets[(size_t) k]);
    pickerOpen = false;
    commit (pickerItem, sel);
}

void WheelOverlay::beginClose()
{
    if (! closing)
    {
        closing = true;
        sound (UISounds::close);
    }
}

//==============================================================================
void WheelOverlay::updateHover (juce::Point<float> pos)
{
    if (closing || pickerOpen)
        return;

    const auto q = toDesign (pos);
    const float r = q.getDistanceFromOrigin();

    if ((inCentre (q) && ! dragging) || r < RI - 4.0f)
    {
        if (! searching) setHover (-1);
        return;
    }
    if (r > RO + (dragging ? 40.0f : 60.0f))
    {
        if (dragging) setHover (-1);
        return;
    }

    const int i = segmentAt (q);
    if (dragging || ! currentItems()[i].isEmpty())
        setHover (i);
}

void WheelOverlay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    updateHover (e.position);
}

void WheelOverlay::rightClick (juce::Point<float> pos)
{
    if (pickerOpen)  { closePicker(); return; }
    if (dragging)    { endDrag (false); return; }
    if (searching)   { closeSearch(); return; }
    if (inFolder)    { back(); return; }

    const auto q = toDesign (pos);
    const float r = q.getDistanceFromOrigin();
    if (r >= RI && r <= RO + 14.0f)
        clearSlot (segmentAt (q));
}

void WheelOverlay::mouseDown (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (closing)
        return;

    if (e.mods.isPopupMenu())
    {
        rightClick (e.position);
        return;
    }

    for (auto& h : hotspots)
        if (h.r.contains (e.position))
        {
            auto action = h.action;
            action();
            return;
        }

    if (pickerOpen)
    {
        if (! pickerRect.contains (e.position))
            closePicker();
        return;
    }

    for (size_t k = 0; k < recentRects.size(); ++k)
        if (recentRects[k].contains (e.position) && (int) k < recents.size())
        {
            pending = { -1, recents[(int) k], e.position, true };
            return;
        }

    const auto q = toDesign (e.position);
    if (inCentre (q))
    {
        openSearch();
        return;
    }

    const float r = q.getDistanceFromOrigin();
    if (r >= RI - 6.0f && r <= RO + 14.0f)
    {
        const int i = segmentAt (q);
        pending = { i, currentItems()[i], e.position, true };
        return;
    }

    beginClose();   // click on the backdrop closes the wheel
}

void WheelOverlay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (pending.active && ! dragging && e.position.getDistanceFrom (pending.start) > 7.0f)
    {
        if (pending.item.isPlugin()) startDrag (pending.item);
        else                         pending.active = false;
    }
    if (dragging)
        needsRepaint = true;
    updateHover (e.position);
}

void WheelOverlay::mouseUp (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragging)
    {
        endDrag (true);
        return;
    }

    if (pending.active)
    {
        const auto p = pending;
        pending.active = false;
        if (p.seg < 0) choose (p.item);
        else           activate (p.seg);
    }
}

void WheelOverlay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int pages = numPages();
    if (pages <= 1 || w.deltaY == 0.0f)
        return;
    const int np = juce::jlimit (0, pages - 1, page + (w.deltaY < 0 ? 1 : -1));
    if (np != page)
    {
        page = np;
        hover = -1;
        contentChanged();
        sound (UISounds::tick);
    }
}

bool WheelOverlay::keyPressed (const juce::KeyPress& k)
{
    if (closing)
        return true;

    const int code = k.getKeyCode();
    const auto ch = k.getTextCharacter();
    const bool isDigit = ch >= '0' && ch <= '9';
    const int digitIndex = isDigit ? (ch == '0' ? 9 : (int) (ch - '1')) : -1;
    const bool printable = ch >= ' ' && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isCtrlDown();

    if (pickerOpen)
    {
        if (code == juce::KeyPress::escapeKey)        closePicker();
        else if (code == juce::KeyPress::returnKey)   confirmPicker();
        else if (isDigit)                             togglePick (digitIndex);
        else if (ch == 'a' || ch == 'A')              { for (int i = 0; i < (int) targets.size(); ++i) picked.insert (i); sound (UISounds::toggle); }
        needsRepaint = true;
        return true;
    }

    if (code == juce::KeyPress::tabKey) { beginClose(); return true; }

    if (dragging)
    {
        if (code == juce::KeyPress::escapeKey) endDrag (false);
        return true;
    }

    const int n = currentItems().size();
    auto moveHover = [&] (int delta)
    {
        if (n == 0) return;
        int h = hover < 0 ? (delta > 0 ? -1 : 0) : hover;
        for (int tries = 0; tries < N; ++tries)
        {
            h = (h + delta + N) % N;
            if (! currentItems()[h].isEmpty()) { setHover (h); break; }
        }
    };

    if (searching)
    {
        if (code == juce::KeyPress::escapeKey)            closeSearch();
        else if (code == juce::KeyPress::returnKey)       { if (hover >= 0) activate (hover); }
        else if (code == juce::KeyPress::backspaceKey)
        {
            if (query.isEmpty()) closeSearch();
            else { query = query.dropLastCharacters (1); keySound (0, true); runSearch(); }
        }
        else if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)  moveHover (1);
        else if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::upKey)    moveHover (-1);
        else if (printable)                               { query += juce::String::charToString (ch); keySound (ch); runSearch(); }
        needsRepaint = true;
        return true;
    }

    if (code == juce::KeyPress::escapeKey)                { if (inFolder) back(); else beginClose(); }
    else if (code == juce::KeyPress::backspaceKey)        back();
    else if (code == juce::KeyPress::returnKey)           { if (hover >= 0) activate (hover); }
    else if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)  moveHover (1);
    else if (code == juce::KeyPress::leftKey  || code == juce::KeyPress::upKey)    moveHover (-1);
    else if (isDigit)
    {
        if (inFolder) { inFolder = false; page = 0; contentChanged(); }
        activate (digitIndex);
    }
    else if (printable && ch != ' ')
    {
        openSearch();
        query += juce::String::charToString (ch);
        keySound (ch);
        runSearch();
    }
    needsRepaint = true;
    return true;
}

//==============================================================================
void WheelOverlay::timerCallback()
{
    if (! *ownerAlive)
    {
        stopTimer();
        juce::MessageManager::callAsync ([] { currentOverlay().reset(); });
        return;
    }

    const auto items = currentItems();
    float motion = 0.0f;

    for (int i = 0; i < N; ++i)
    {
        const bool has = ! items[i].isEmpty();
        const float target = (i == hover && (has || dragging)) ? 1.0f : 0.0f;
        HV[i] = HV[i] * 0.74f + (target - H[i]) * 0.2f;
        H[i] += HV[i];
        const float et = (has || (dragging && i == hover)) ? 0.0f : 1.0f;
        E[i] += (et - E[i]) * 0.15f;
        motion += std::abs (HV[i]) + std::abs (et - E[i]);
    }

    MV = MV * 0.8f + (((searching && ! dragging) ? 1.0f : 0.0f) - M) * 0.11f;
    M += MV;
    press *= 0.88f;

    const float openTarget = closing ? 0.0f : (pickerOpen ? 0.55f : 1.0f);
    openA += (openTarget - openA) * (closing ? 0.28f : 0.2f);
    labelA += (1.0f - labelA) * 0.22f;

    motion += std::abs (MV) + press + std::abs (openTarget - openA) + (1.0f - labelA);

    if (closing && openA < 0.02f)
    {
        stopTimer();
        const bool showPlugin = openPluginAfterClose;
        auto* o = &owner;
        auto alive = ownerAlive;
        juce::MessageManager::callAsync ([showPlugin, o, alive]
        {
            currentOverlay().reset();
            if (showPlugin && *alive)
                o->showPluginWindow();
        });
        return;
    }

    layout();

    {
        const juce::SpinLock::ScopedLockType sl (uniformLock);
        uniforms.w = (float) getWidth();
        uniforms.h = (float) getHeight();
        uniforms.cx = centre().x;
        uniforms.cy = centre().y;
        uniforms.scale = wheelScale();
        std::copy (std::begin (H), std::end (H), uniforms.H);
        std::copy (std::begin (E), std::end (E), uniforms.E);
        uniforms.M = M;
        uniforms.press = press;
        uniforms.openA = openA;
        uniforms.mx = mousePos.x;
        uniforms.my = mousePos.y;
        uniforms.numGlass = (int) juce::jmin ((size_t) maxGlass, glass.size());
        for (int i = 0; i < uniforms.numGlass; ++i)
        {
            const auto& gr = glass[(size_t) i];
            uniforms.rects[i * 4 + 0] = gr.r.getX();
            uniforms.rects[i * 4 + 1] = gr.r.getY();
            uniforms.rects[i * 4 + 2] = gr.r.getWidth();
            uniforms.rects[i * 4 + 3] = gr.r.getHeight();
            uniforms.rectParams[i * 4 + 0] = gr.radius;
            uniforms.rectParams[i * 4 + 1] = gr.alpha;
            uniforms.rectParams[i * 4 + 2] = gr.dark;
            uniforms.rectParams[i * 4 + 3] = gr.tint;
        }
    }

    const bool toastVisible = juce::Time::getMillisecondCounterHiRes() < toastUntil + 300.0;
    if (needsRepaint || motion > 0.0005f || toastVisible || searching)
    {
        needsRepaint = false;
        repaint();
    }
}

//==============================================================================
void WheelOverlay::layout()
{
    glass.clear();
    hotspots.clear();
    recentRects.clear();
    pickerRows.clear();

    const auto bounds = getLocalBounds().toFloat();
    const auto c = centre();
    const float sc = animScale();
    const float a = openA;

    auto addGlass = [this] (juce::Rectangle<float> r, float radius, float alpha, float dark = 0.0f, float tint = 0.0f)
    {
        glass.push_back ({ r, radius, alpha, dark, tint });
    };
    auto hovered = [this] (juce::Rectangle<float> r) { return r.contains (mousePos) ? 0.08f : 0.0f; };

    // breadcrumb
    crumbRect = {};
    if (inFolder && ! searching && ! dragging)
    {
        const auto txt = U ("\xe2\x80\xb9  ") + folderName;
        const float w = textWidth (font (12.5f), txt) + 32.0f;
        crumbRect = { c.x - w * 0.5f, c.y - 260.0f * sc - 44.0f, w, 30.0f };
        addGlass (crumbRect, 15.0f, a, 0.0f, hovered (crumbRect));
        hotspots.push_back ({ crumbRect, [this] { back(); } });
    }

    // page indicator
    if (numPages() > 1)
    {
        const auto txt = "Page " + juce::String (page + 1) + " / " + juce::String (numPages()) + U ("  \xc2\xb7  scroll");
        const float w = textWidth (font (11.0f), txt) + 28.0f;
        addGlass ({ c.x - w * 0.5f, c.y + 252.0f * sc, w, 26.0f }, 13.0f, a);
    }

    // recents
    {
        const int n = juce::jmax (1, recents.size());
        const float h = 34.0f, gap = 8.0f;
        float y = c.y - (20.0f + (float) n * (h + gap)) * 0.5f + 20.0f;
        for (int k = 0; k < n; ++k)
        {
            const juce::Rectangle<float> r (18.0f, y, 190.0f, h);
            addGlass (r, h * 0.5f, a * (recents.isEmpty() ? 0.55f : 1.0f), 0.0f, recents.isEmpty() ? 0.0f : hovered (r));
            recentRects.push_back (r);
            y += h + gap;
        }
    }

    // help pill
    {
        const float w = 560.0f;
        addGlass ({ bounds.getCentreX() - w * 0.5f, bounds.getBottom() - 46.0f, w, 30.0f }, 15.0f, a);
    }

    // tools (bottom right)
    {
        const juce::StringArray labels { "Send to  " + juce::String (lib->pickTracks ? "Pick tracks" : "This track"),
                                         "Sound  " + juce::String (lib->soundEnabled ? "On" : "Off"),
                                         U ("\xe2\x86\xba  Reset slots") };
        float y = bounds.getBottom() - 16.0f - 30.0f;
        toolRects.assign (3, {});
        for (int i = labels.size(); --i >= 0;)
        {
            const float w = textWidth (font (11.0f), labels[i]) + 28.0f;
            const juce::Rectangle<float> r (bounds.getRight() - 16.0f - w, y, w, 30.0f);
            addGlass (r, 15.0f, a, 0.0f, hovered (r));
            toolRects[(size_t) i] = r;
            std::function<void()> act;
            if (i == 0) act = [this] { lib->pickTracks = ! lib->pickTracks; lib->savePrefs(); sound (UISounds::toggle); needsRepaint = true; };
            if (i == 1) act = [this] { lib->soundEnabled = ! lib->soundEnabled; lib->savePrefs(); sound (UISounds::toggle); needsRepaint = true; };
            if (i == 2) act = [this] { lib->resetSlots(); inFolder = false; page = 0; contentChanged(); sound (UISounds::pin); toast ("Reset", "default slots"); };
            hotspots.push_back ({ r, act });
            y -= 38.0f;
        }
    }

    // toast
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now < toastUntil + 250.0)
        {
            const float fade = (float) juce::jlimit (0.0, 1.0, (toastUntil + 250.0 - now) / 250.0);
            const float w = textWidth (font (13.0f), toastLabel + "   " + toastText) + 44.0f;
            addGlass ({ bounds.getCentreX() - w * 0.5f, 38.0f, w, 36.0f }, 18.0f, fade);
        }
    }

    // drag ghost
    if (dragging)
    {
        const float w = textWidth (font (13.0f), dragItem.name) + 32.0f;
        addGlass ({ mousePos.x + 14.0f, mousePos.y + 10.0f, w, 32.0f }, 16.0f, 1.0f, 0.0f, 0.1f);
    }

    // track picker
    pickerRect = {};
    if (pickerOpen)
    {
        const int rows = ((int) targets.size() + 1) / 2;
        const float w = juce::jmin (460.0f, bounds.getWidth() - 40.0f);
        const float h = 20.0f + 56.0f + (float) rows * 45.0f + 10.0f + 36.0f + 20.0f;
        pickerRect = { c.x - w * 0.5f, c.y - h * 0.5f, w, h };
        addGlass (pickerRect, 22.0f, 1.0f, 0.35f);

        const float colW = (w - 40.0f - 7.0f) * 0.5f;
        for (int k = 0; k < (int) targets.size(); ++k)
        {
            const juce::Rectangle<float> r (pickerRect.getX() + 20.0f + (float) (k % 2) * (colW + 7.0f),
                                            pickerRect.getY() + 76.0f + (float) (k / 2) * 45.0f, colW, 38.0f);
            pickerRows.push_back (r);
            hotspots.push_back ({ r, [this, k] { togglePick (k); } });
        }

        const float by = pickerRect.getBottom() - 20.0f - 34.0f;
        auto btn = [&] (float x, const juce::String& t) { return juce::Rectangle<float> (x, by, textWidth (font (12.0f), t) + 28.0f, 34.0f); };
        pickerAll  = btn (pickerRect.getX() + 20.0f, "All");
        pickerNone = btn (pickerAll.getRight() + 8.0f, "None");
        const auto goText = picked.empty() ? juce::String ("Load") : "Load (" + juce::String ((int) picked.size()) + ")";
        pickerGo = btn (0.0f, goText);
        pickerGo.setX (pickerRect.getRight() - 20.0f - pickerGo.getWidth());
        pickerCancel = btn (0.0f, "Cancel");
        pickerCancel.setX (pickerGo.getX() - 8.0f - pickerCancel.getWidth());

        hotspots.push_back ({ pickerAll,    [this] { for (int i = 0; i < (int) targets.size(); ++i) picked.insert (i); sound (UISounds::toggle); } });
        hotspots.push_back ({ pickerNone,   [this] { picked.clear(); sound (UISounds::toggle); } });
        hotspots.push_back ({ pickerCancel, [this] { closePicker(); } });
        hotspots.push_back ({ pickerGo,     [this] { confirmPicker(); } });
    }
}

//==============================================================================
void WheelOverlay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto c = centre();
    const float sc = animScale();
    const float a = juce::jlimit (0.0f, 1.0f, openA);
    const auto white = juce::Colours::white;
    const auto items = currentItems();

    // --- ring: numbers + labels
    const float la = a * labelA;
    for (int i = 0; i < N; ++i)
    {
        const auto d = dirOf (i);
        const float off = H[i] * 9.0f;
        const bool on = (i == hover) && (! items[i].isEmpty() || dragging);

        const auto np = c + d * ((RO - 17.0f + off) * sc);
        g.setColour (white.withAlpha (a * (on ? 1.0f : 0.35f)));
        g.drawEllipse (np.x - 7.5f * sc, np.y - 7.5f * sc, 15.0f * sc, 15.0f * sc, 1.0f);
        g.setFont (font (8.0f * sc));
        g.setColour (white.withAlpha (a * (on ? 1.0f : 0.65f)));
        g.drawText (juce::String ((i + 1) % 10), juce::Rectangle<float> (np.x - 10, np.y - 10, 20, 20), juce::Justification::centred, false);

        const auto lp = c + d * ((LR + off) * sc);
        const auto& it = items[i];
        const juce::Rectangle<float> lr (lp.x - 80.0f * sc, lp.y - 12.0f * sc, 160.0f * sc, 24.0f * sc);

        if (it.isEmpty())
        {
            if (dragging)
            {
                g.setFont (font (11.0f * sc, false));
                shadowText (g, "Empty slot", lr, juce::Justification::centred, white.withAlpha (0.45f * la));
            }
            continue;
        }

        const auto f = font (12.5f * sc);
        g.setFont (f);
        auto name = fit (it.name);
        const auto lowerQ = query.toLowerCase();
        const int k = (searching && query.isNotEmpty() && ! dragging) ? name.toLowerCase().indexOf (lowerQ) : -1;

        if (k >= 0)
        {
            const auto pre = name.substring (0, k), mid = name.substring (k, k + query.length()), post = name.substring (k + query.length());
            const float wPre = textWidth (f, pre), wMid = textWidth (f, mid), wPost = textWidth (f, post);
            float x = lp.x - (wPre + wMid + wPost) * 0.5f;
            const auto row = [&] (const juce::String& s, float w, float alpha)
            {
                shadowText (g, s, { x, lr.getY(), w + 2.0f, lr.getHeight() }, juce::Justification::centredLeft, white.withAlpha (alpha * la));
                x += w;
            };
            row (pre, wPre, 0.55f);
            row (mid, wMid, 1.0f);
            row (post, wPost, 0.55f);
        }
        else
        {
            if (it.isFolder())
                name += U (" \xe2\x80\xba");
            shadowText (g, name, lr, juce::Justification::centred, white.withAlpha (0.93f * la));
        }
    }

    // --- centre button / search bar
    const float m = juce::jlimit (0.0f, 1.0f, M);
    const float btnA = juce::jmax (0.0f, 1.0f - m * 2.2f) * a;
    if (btnA > 0.01f)
    {
        g.setColour (white.withAlpha (btnA));
        g.strokePath (magnifier (c + juce::Point<float> (0.0f, -12.0f * sc), 22.0f * sc), juce::PathStrokeType (2.2f * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (font (13.0f * sc));
        shadowText (g, dragging ? "Drop on a slot" : "Type to search",
                    { c.x - 80.0f * sc, c.y + 4.0f * sc, 160.0f * sc, 20.0f * sc }, juce::Justification::centred, white.withAlpha (btnA));
        if (dragging)
        {
            g.setFont (font (10.0f * sc, false));
            shadowText (g, "Esc to cancel", { c.x - 80.0f * sc, c.y + 22.0f * sc, 160.0f * sc, 16.0f * sc }, juce::Justification::centred, white.withAlpha (0.7f * btnA));
        }
    }

    const float inA = juce::jmax (0.0f, m * 2.0f - 1.0f) * a;
    if (inA > 0.01f)
    {
        const float hx = centreHalfSize().x * sc;
        const float left = c.x - hx + 20.0f * sc;
        g.setColour (white.withAlpha (0.85f * inA));
        g.strokePath (magnifier ({ left + 9.0f * sc, c.y }, 18.0f * sc), juce::PathStrokeType (2.0f * sc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto f = font (15.0f * sc, false);
        g.setFont (f);
        const float tx = left + 28.0f * sc;
        if (query.isEmpty())
            shadowText (g, "Search plugins...", { tx, c.y - 12.0f * sc, 240.0f * sc, 24.0f * sc }, juce::Justification::centredLeft, white.withAlpha (0.6f * inA));
        else
            shadowText (g, query, { tx, c.y - 12.0f * sc, 260.0f * sc, 24.0f * sc }, juce::Justification::centredLeft, white.withAlpha (inA));

        if (std::fmod (juce::Time::getMillisecondCounterHiRes(), 1000.0) < 520.0)
        {
            const float cx = tx + textWidth (f, query) + 1.5f;
            g.setColour (white.withAlpha (inA));
            g.fillRect (cx, c.y - 9.0f * sc, 1.5f, 18.0f * sc);
        }

        if (query.isNotEmpty())
        {
            const int n = searchResults.size();
            auto cnt = n == 0 ? juce::String ("no results") : juce::String (n) + (n == 1 ? " result" : " results");
            g.setFont (font (11.0f * sc, false));
            shadowText (g, cnt, { c.x + hx - 130.0f * sc, c.y - 10.0f * sc, 110.0f * sc, 20.0f * sc }, juce::Justification::centredRight, white.withAlpha (0.7f * inA));
        }
    }

    // --- breadcrumb + page indicator
    if (! crumbRect.isEmpty())
    {
        g.setFont (font (12.5f));
        shadowText (g, U ("\xe2\x80\xb9  ") + folderName, crumbRect, juce::Justification::centred, white.withAlpha (a));
    }
    if (numPages() > 1)
    {
        g.setFont (font (11.0f, false));
        shadowText (g, "Page " + juce::String (page + 1) + " / " + juce::String (numPages()) + U ("  \xc2\xb7  scroll"),
                    { c.x - 120.0f, c.y + 252.0f * sc, 240.0f, 26.0f }, juce::Justification::centred, white.withAlpha (0.8f * a));
    }

    // --- recents
    if (! recentRects.empty())
    {
        g.setFont (font (10.0f));
        shadowText (g, "RECENT", { 24.0f, recentRects.front().getY() - 22.0f, 120.0f, 16.0f }, juce::Justification::centredLeft, white.withAlpha (0.6f * a));

        for (size_t k = 0; k < recentRects.size(); ++k)
        {
            const auto r = recentRects[k].reduced (14.0f, 0.0f);
            if (recents.isEmpty())
            {
                g.setFont (font (11.0f, false));
                shadowText (g, "Nothing loaded yet", r, juce::Justification::centredLeft, white.withAlpha (0.6f * a));
                break;
            }
            const auto& it = recents[(int) k];
            g.setFont (font (9.5f, false));
            const float subW = juce::jmin (70.0f, textWidth (font (9.5f, false), it.sub) + 2.0f);
            shadowText (g, it.sub, r.withTrimmedLeft (r.getWidth() - subW), juce::Justification::centredRight, white.withAlpha (0.55f * a));
            g.setFont (font (12.5f));
            shadowText (g, fit (it.name, 18), r.withTrimmedRight (subW + 6.0f), juce::Justification::centredLeft, white.withAlpha (a));
        }
    }

    // --- help pill
    {
        const juce::StringArray keys  { "TAB", U ("1\xe2\x80\x93" "0"), U ("A\xe2\x80\x93Z"), "drag", "right-click", "scroll" };
        const juce::StringArray words { "close", "slots", "search", "pin", "clear / back", "pages" };
        const auto fb = font (11.0f), fr = font (11.0f, false);
        float total = 0.0f;
        for (int i = 0; i < keys.size(); ++i)
            total += textWidth (fb, keys[i]) + 4.0f + textWidth (fr, words[i]) + (i < keys.size() - 1 ? 14.0f : 0.0f);
        float x = bounds.getCentreX() - total * 0.5f;
        const float y = bounds.getBottom() - 46.0f;
        for (int i = 0; i < keys.size(); ++i)
        {
            g.setFont (fb);
            const float wk = textWidth (fb, keys[i]);
            shadowText (g, keys[i], { x, y, wk + 2.0f, 30.0f }, juce::Justification::centredLeft, white.withAlpha (a));
            x += wk + 4.0f;
            g.setFont (fr);
            const float ww = textWidth (fr, words[i]);
            shadowText (g, words[i], { x, y, ww + 2.0f, 30.0f }, juce::Justification::centredLeft, white.withAlpha (0.78f * a));
            x += ww + 14.0f;
        }
    }

    // --- tools text
    {
        const juce::StringArray dims { "Send to", "Sound", "" };
        const juce::StringArray vals { lib->pickTracks ? "Pick tracks" : "This track", lib->soundEnabled ? "On" : "Off", U ("\xe2\x86\xba  Reset slots") };
        for (int i = 0; i < (int) toolRects.size() && i < 3; ++i)
        {
            const auto r = toolRects[(size_t) i].reduced (14.0f, 0.0f);
            const auto fd = font (11.0f, false);
            const float wd = dims[i].isEmpty() ? 0.0f : textWidth (fd, dims[i]) + 6.0f;
            g.setFont (fd);
            shadowText (g, dims[i], r, juce::Justification::centredLeft, white.withAlpha (0.6f * a));
            g.setFont (font (11.0f));
            shadowText (g, vals[i], r.withTrimmedLeft (wd), juce::Justification::centredLeft, white.withAlpha (a));
        }
    }

    // --- credit
    g.setFont (font (10.0f));
    shadowText (g, "@buyorbealoser", { 18.0f, bounds.getBottom() - 34.0f, 200.0f, 16.0f }, juce::Justification::centredLeft, white.withAlpha (0.32f));

    // --- toast
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now < toastUntil + 250.0)
        {
            const float fade = (float) juce::jlimit (0.0, 1.0, (toastUntil + 250.0 - now) / 250.0);
            const auto fl = font (13.0f, false), ft = font (13.0f);
            const float wl = textWidth (fl, toastLabel), wt = textWidth (ft, toastText);
            float x = bounds.getCentreX() - (wl + 10.0f + wt) * 0.5f;
            g.setFont (fl);
            shadowText (g, toastLabel, { x, 38.0f, wl + 2.0f, 36.0f }, juce::Justification::centredLeft, white.withAlpha (0.6f * fade));
            g.setFont (ft);
            shadowText (g, toastText, { x + wl + 10.0f, 38.0f, wt + 2.0f, 36.0f }, juce::Justification::centredLeft, white.withAlpha (fade));
        }
    }

    // --- drag ghost
    if (dragging)
    {
        g.setFont (font (13.0f));
        const float w = textWidth (font (13.0f), dragItem.name) + 32.0f;
        shadowText (g, dragItem.name, { mousePos.x + 14.0f, mousePos.y + 10.0f, w, 32.0f }, juce::Justification::centred, white);
    }

    // --- track picker
    if (pickerOpen && ! pickerRect.isEmpty())
    {
        const auto r = pickerRect.reduced (20.0f);
        const auto ff = font (15.0f, false), fb = font (15.0f);
        float x = r.getX();
        g.setFont (ff); shadowText (g, "Send ", { x, r.getY(), 60.0f, 22.0f }, juce::Justification::centredLeft, white); x += textWidth (ff, "Send ");
        g.setFont (fb); shadowText (g, pickerItem.name, { x, r.getY(), 300.0f, 22.0f }, juce::Justification::centredLeft, white); x += textWidth (fb, pickerItem.name);
        g.setFont (ff); shadowText (g, " to...", { x, r.getY(), 80.0f, 22.0f }, juce::Justification::centredLeft, white);

        g.setFont (font (11.0f, false));
        shadowText (g, U ("Toggle tracks with a click or keys 1\xe2\x80\x93" "0  \xc2\xb7  A all  \xc2\xb7  Enter load  \xc2\xb7  Esc cancel"),
                    { r.getX(), r.getY() + 24.0f, r.getWidth(), 18.0f }, juce::Justification::centredLeft, white.withAlpha (0.6f));

        for (size_t k = 0; k < pickerRows.size(); ++k)
        {
            const auto row = pickerRows[k];
            const bool on = picked.count ((int) k) > 0;
            g.setColour (white.withAlpha (on ? 0.22f : (row.contains (mousePos) ? 0.12f : 0.06f)));
            g.fillRoundedRectangle (row, 12.0f);
            g.setColour (white.withAlpha (on ? 0.55f : 0.1f));
            g.drawRoundedRectangle (row.reduced (0.5f), 12.0f, 1.0f);

            const auto& t = targets[k];
            g.setColour (t.isOwner ? white : colourFor (t.name));
            g.fillEllipse (row.getX() + 11.0f, row.getCentreY() - 4.5f, 9.0f, 9.0f);

            g.setFont (font (12.5f));
            shadowText (g, t.name + (t.isOwner ? "  (this)" : ""), { row.getX() + 29.0f, row.getY(), row.getWidth() - 80.0f, row.getHeight() },
                        juce::Justification::centredLeft, white);

            if (k < 10)
            {
                g.setFont (font (9.5f));
                g.setColour (white.withAlpha (0.5f));
                g.drawText (juce::String ((k + 1) % 10), juce::Rectangle<float> (row.getRight() - 46.0f, row.getY(), 14.0f, row.getHeight()), juce::Justification::centred, false);
            }

            const juce::Rectangle<float> ck (row.getRight() - 26.0f, row.getCentreY() - 7.5f, 15.0f, 15.0f);
            if (on)
            {
                g.setColour (white);
                g.fillEllipse (ck);
                juce::Path tick;
                tick.startNewSubPath (ck.getX() + 4.0f, ck.getCentreY());
                tick.lineTo (ck.getX() + 6.8f, ck.getBottom() - 4.2f);
                tick.lineTo (ck.getRight() - 3.8f, ck.getY() + 4.5f);
                g.setColour (juce::Colour (0xff111111));
                g.strokePath (tick, juce::PathStrokeType (1.6f));
            }
            else
            {
                g.setColour (white.withAlpha (0.45f));
                g.drawEllipse (ck.reduced (0.75f), 1.5f);
            }
        }

        auto button = [&] (juce::Rectangle<float> b, const juce::String& t, bool main, bool enabled)
        {
            const float alpha = enabled ? 1.0f : 0.35f;
            g.setColour (main ? white.withAlpha (0.9f * alpha) : white.withAlpha ((b.contains (mousePos) ? 0.16f : 0.08f) * alpha));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (white.withAlpha (0.18f * alpha));
            g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
            g.setFont (font (12.0f));
            g.setColour (main ? juce::Colour (0xff111111).withAlpha (alpha) : white.withAlpha (alpha));
            g.drawText (t, b, juce::Justification::centred, false);
        };
        button (pickerAll, "All", false, true);
        button (pickerNone, "None", false, true);
        button (pickerCancel, "Cancel", false, true);
        button (pickerGo, picked.empty() ? juce::String ("Load") : "Load (" + juce::String ((int) picked.size()) + ")", true, ! picked.empty());
    }
}
