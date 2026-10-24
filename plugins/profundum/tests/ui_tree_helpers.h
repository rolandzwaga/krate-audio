#pragma once

// ==============================================================================
// Profundum - test UI-tree helpers (Phase 12 T017)
// ==============================================================================
// Header-only, every function inline, namespace ProfundumPluginTest.
//
//   - HeadlessEditor: RAII headless VST3Editor on resources/editor.uidesc, built
//     exactly as editor_lifecycle_test.cpp does (absolute uidesc path, then
//     attached(nullptr, nativePlatformType()) builds the tree and fires didOpen);
//     the destructor calls removed() (fires willClose) and release().
//   - collectBoundControls: copied from unit/controller/editor_lifecycle_test.cpp
//     (every CControl with tag >= 0; CTextLabel IS-A CControl and keeps tag -1).
//   - collectViewsOfType<T>: every descendant view that dynamic_casts to T.
//   - XML scan (LayoutRect / XmlNode / XmlScan / scanUidesc / findTemplate /
//     isDescendantOf / findLabelled / boundNodes): copied from
//     plugins/vorago/tests/unit/controller/editor_layout_test.cpp (Deviation 7).
//
// NEVER name a kPlatformType* constant here - always
// Krate::TestSupport::nativePlatformType() (lint-platform-type-literals.js).
// ==============================================================================

#include "test_helpers/editor_lifecycle_harness.h"

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ProfundumPluginTest {

// ==============================================================================
// Headless editor (built tree)
// ==============================================================================

struct HeadlessEditor {
    explicit HeadlessEditor(Steinberg::Vst::EditController* controller) {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc";
        editor_ = new VSTGUI::VST3Editor(controller, "editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor_;
        attached_ = view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==
                    Steinberg::kResultTrue;
    }

    ~HeadlessEditor() {
        if (editor_ == nullptr) {
            return;
        }
        Steinberg::IPlugView* view = editor_;
        if (attached_) {
            view->removed();
        }
        view->release();
    }

    HeadlessEditor(const HeadlessEditor&) = delete;
    HeadlessEditor& operator=(const HeadlessEditor&) = delete;
    HeadlessEditor(HeadlessEditor&&) = delete;
    HeadlessEditor& operator=(HeadlessEditor&&) = delete;

    [[nodiscard]] VSTGUI::VST3Editor* editor() const { return editor_; }
    [[nodiscard]] VSTGUI::CFrame* frame() const {
        return editor_ == nullptr ? nullptr : editor_->getFrame();
    }

private:
    VSTGUI::VST3Editor* editor_ = nullptr;
    bool attached_ = false;
};

/// Recursive walk collecting every CControl bound to a parameter (tag >= 0).
inline void collectBoundControls(VSTGUI::CViewContainer* container,
                                 std::vector<VSTGUI::CControl*>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(view)) {
            if (control->getTag() >= 0) {
                out.push_back(control);
            }
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectBoundControls(child, out);
        }
    }
}

namespace detail {
template <class T>
inline void collectViewsOfTypeInto(VSTGUI::CViewContainer* container, std::vector<T*>& out) {
    if (container == nullptr) {
        return;
    }
    const std::uint32_t count = container->getNbViews();
    for (std::uint32_t i = 0; i < count; ++i) {
        VSTGUI::CView* view = container->getView(i);
        if (view == nullptr) {
            continue;
        }
        if (auto* typed = dynamic_cast<T*>(view)) {
            out.push_back(typed);
        }
        if (auto* child = dynamic_cast<VSTGUI::CViewContainer*>(view)) {
            collectViewsOfTypeInto(child, out);
        }
    }
}
}  // namespace detail

/// Every descendant of `container` (not the container itself) that is a T.
template <class T>
[[nodiscard]] inline std::vector<T*> collectViewsOfType(VSTGUI::CViewContainer* container) {
    std::vector<T*> out;
    detail::collectViewsOfTypeInto(container, out);
    return out;
}

// ==============================================================================
// Stack-based uidesc element scan
// ==============================================================================

struct LayoutRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    [[nodiscard]] double width() const { return right - left; }
    [[nodiscard]] double height() const { return bottom - top; }
    [[nodiscard]] bool contains(const LayoutRect& o) const {
        return o.left >= left && o.top >= top && o.right <= right && o.bottom <= bottom;
    }
    [[nodiscard]] bool operator==(const LayoutRect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
};

// NOLINTNEXTLINE(bugprone-exception-escape) -- implicit special members of std::map/std::string members; test-only value type
struct XmlNode {
    std::string element;  ///< "view" or "template"
    std::map<std::string, std::string> attrs;
    int parent = -1;
    std::vector<int> children;
    LayoutRect window;  ///< resolved window coordinates

    [[nodiscard]] bool has(const std::string& key) const { return attrs.contains(key); }
    [[nodiscard]] std::string get(const std::string& key) const {
        const auto it = attrs.find(key);
        return it == attrs.end() ? std::string{} : it->second;
    }
};

struct XmlScan {
    std::vector<XmlNode> nodes;
    bool wellFormed = true;
};

/// "x, y" -> {x, y}. strtod, not sscanf (MSVC C4996).
[[nodiscard]] inline bool parsePoint(const std::string& s, double& x, double& y) {
    const char* p = s.c_str();
    char* end = nullptr;
    x = std::strtod(p, &end);
    if (end == p) {
        return false;
    }
    p = end;
    while (*p == ' ' || *p == ',') {
        ++p;
    }
    y = std::strtod(p, &end);
    return end != p;
}

/// The five predefined XML entities (UIAttributes values arrive decoded).
[[nodiscard]] inline std::string decodeEntities(const std::string& in) {
    static const std::array<std::pair<const char*, char>, 5> kEntities = {{
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\x27'},
    }};
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        bool replaced = false;
        if (in[i] == '&') {
            for (const auto& [entity, ch] : kEntities) {
                const std::size_t len = std::strlen(entity);
                if (in.compare(i, len, entity) == 0) {
                    out.push_back(ch);
                    i += len;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) {
            out.push_back(in[i]);
            ++i;
        }
    }
    return out;
}

[[nodiscard]] inline bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == ':';
}

inline XmlScan scanUidesc(const std::string& xml) {
    XmlScan scan;
    std::vector<int> stack;
    std::size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        if (xml.compare(pos, 4, "<!--") == 0) {
            const std::size_t e = xml.find("-->", pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            pos = e + 3;
            continue;
        }
        if (xml.compare(pos, 2, "<?") == 0) {
            const std::size_t e = xml.find("?>", pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            pos = e + 2;
            continue;
        }
        if (xml.compare(pos, 2, "</") == 0) {
            const std::size_t e = xml.find('>', pos);
            if (e == std::string::npos) {
                scan.wellFormed = false;
                break;
            }
            std::size_t n = pos + 2;
            std::string name;
            while (n < e && isNameChar(xml[n])) {
                name.push_back(xml[n++]);
            }
            if (name == "view" || name == "template") {
                if (stack.empty() || scan.nodes[static_cast<std::size_t>(stack.back())].element != name) {
                    scan.wellFormed = false;
                } else {
                    stack.pop_back();
                }
            }
            pos = e + 1;
            continue;
        }

        // Opening (or self-closing) element: name, then quoted attributes up to
        // an UNQUOTED '>' (a tooltip may legitimately contain '>').
        std::size_t p = pos + 1;
        std::string name;
        while (p < xml.size() && isNameChar(xml[p])) {
            name.push_back(xml[p++]);
        }
        std::map<std::string, std::string> attrs;
        bool selfClosing = false;
        bool closed = false;
        while (p < xml.size()) {
            const char c = xml[p];
            if (c == '>') {
                closed = true;
                ++p;
                break;
            }
            if (c == '/') {
                selfClosing = true;
                ++p;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++p;
                continue;
            }
            std::string key;
            while (p < xml.size() && isNameChar(xml[p])) {
                key.push_back(xml[p++]);
            }
            if (key.empty()) {
                ++p;  // stray character; skip it
                continue;
            }
            while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p])) != 0) {
                ++p;
            }
            std::string value;
            if (p < xml.size() && xml[p] == '=') {
                ++p;
                while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p])) != 0) {
                    ++p;
                }
                if (p < xml.size() && (xml[p] == '"' || xml[p] == '\'')) {
                    const char q = xml[p++];
                    const std::size_t e = xml.find(q, p);
                    if (e == std::string::npos) {
                        scan.wellFormed = false;
                        p = xml.size();
                        break;
                    }
                    value = decodeEntities(xml.substr(p, e - p));
                    p = e + 1;
                }
            }
            attrs[key] = value;
            selfClosing = false;  // a '/' only counts right before '>'
        }
        if (!closed) {
            scan.wellFormed = false;
            break;
        }
        if (name == "view" || name == "template") {
            XmlNode node;
            node.element = name;
            node.attrs = std::move(attrs);
            node.parent = stack.empty() ? -1 : stack.back();
            const int index = static_cast<int>(scan.nodes.size());
            scan.nodes.push_back(std::move(node));
            if (scan.nodes.back().parent >= 0) {
                scan.nodes[static_cast<std::size_t>(scan.nodes.back().parent)].children.push_back(index);
            }
            if (!selfClosing) {
                stack.push_back(index);
            }
        }
        pos = p;
    }
    if (!stack.empty()) {
        scan.wellFormed = false;
    }

    // Resolve window rects: a template root is the window origin; a view is its
    // parent's window top-left plus its own origin. Parents precede children.
    for (auto& node : scan.nodes) {
        double w = 0.0;
        double h = 0.0;
        (void)parsePoint(node.get("size"), w, h);
        double ox = 0.0;
        double oy = 0.0;
        if (node.parent >= 0) {
            (void)parsePoint(node.get("origin"), ox, oy);
            const LayoutRect& pr = scan.nodes[static_cast<std::size_t>(node.parent)].window;
            ox += pr.left;
            oy += pr.top;
        }
        node.window = LayoutRect{.left = ox, .top = oy, .right = ox + w, .bottom = oy + h};
    }
    return scan;
}

[[nodiscard]] inline int findTemplate(const XmlScan& scan, const std::string& name) {
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        if (scan.nodes[i].element == "template" && scan.nodes[i].get("name") == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] inline bool isDescendantOf(const XmlScan& scan, int node, int ancestor) {
    int p = scan.nodes[static_cast<std::size_t>(node)].parent;
    while (p >= 0) {
        if (p == ancestor) {
            return true;
        }
        p = scan.nodes[static_cast<std::size_t>(p)].parent;
    }
    return false;
}

/// Every node carrying uidesc-label == label.
[[nodiscard]] inline std::vector<int> findLabelled(const XmlScan& scan, const std::string& label) {
    std::vector<int> out;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        if (scan.nodes[i].get("uidesc-label") == label) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

/// Bound view nodes: (node index, resolved ParamID).
[[nodiscard]] inline std::vector<std::pair<int, int>> boundNodes(const XmlScan& scan,
                                                           const std::map<std::string, int>& tagMap) {
    std::vector<std::pair<int, int>> out;
    for (std::size_t i = 0; i < scan.nodes.size(); ++i) {
        const auto& n = scan.nodes[i];
        if (n.element != "view" || !n.has("control-tag")) {
            continue;
        }
        const auto it = tagMap.find(n.get("control-tag"));
        out.emplace_back(static_cast<int>(i), it == tagMap.end() ? -1 : it->second);
    }
    return out;
}

}  // namespace ProfundumPluginTest
