#!/usr/bin/env node
//
// gen-site.js
// =============================================================================
// Builds krateaudio.com into one directory: every released plugin's landing page, user manual and signal-flow page,
// the root index, the sitemap, the 404 page and the shared assets; then runs the SEO lint (tools/lint-site-seo.js)
// over what it wrote and refuses to finish when the lint finds anything.
//
// WHY. docs.yml and release.yml each rendered the landing pages with their own sed block. release.yml's never filled
// {{URL}}, so whichever workflow deployed last decided whether four plugin pages carried a canonical URL or the literal
// text "{{URL}}" (live on krateaudio.com until 2026-11). The sitemap was hand-written and missed Profundum. One
// generator now serves both workflows, the sitemap comes from the same plugin list as the pages, and nothing reaches
// gh-pages without passing the lint.
//
// What a page is built from:
//   landing      plugins/<dir>/docs/index.html with {{VERSION}} {{TAG}} {{REPO}} {{NAME}} {{DESCRIPTION}}
//                {{PUBLISHER}} {{URL}} {{HEAD_COMMON}} {{HERO}} {{OTHER_PLUGINS}}; version from the newest <dir>/v* tag
//   manual       plugins/<dir>/MANUAL.md (first H1 dropped) through pandoc with plugins/<dir>/docs/manual-template.html,
//                then {{HEAD_COMMON}} filled and width / height / lazy loading added to every image
//   signal flow  plugins/<dir>/docs/<dir>_signal_flow.dot via tools/generate-signal-flow.js (pre-rendered to SVG when
//                tools/site/node_modules holds @viz-js/viz, else drawn in the browser as before)
//   root         tools/gen-root-index.js (docs/index-template.html, docs/site.json, the manager card)
//   404          docs/404.html with {{HEAD_COMMON}} and {{PLUGIN_LINKS}}
//   sitemap      every page above, lastmod = the newest commit date of the page's sources (and its release tag)
// A plugin is released when it has a <dir>/v* tag; untagged plugins get no pages.
//
// Usage:
//   node tools/gen-site.js --out <dir> [--no-manuals]
//   run from the repository root with REPO=<owner/name> in the environment (git tags and full history available).
//   --no-manuals skips pandoc (release.yml: the manuals docs.yml deployed stay on gh-pages through keep_files)
// Exit codes: 0 = written and lint-clean, 1 = refused or lint findings (one stderr line each), 2 = usage error.
// =============================================================================
'use strict';

const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const common = require('./site/common.js');
const rootIndex = require('./gen-root-index.js');
const signalFlow = require('./generate-signal-flow.js');
const seoLint = require('./lint-site-seo.js');

const NAME = 'gen-site';
const USAGE = 'usage: node tools/gen-site.js --out <dir> [--no-manuals]';
const PLACEHOLDER_RE = /\{\{[A-Z_]+\}\}/g;
// The editor screenshots in plugins/<p>/images are rendered at 2x (tests/test_helpers/editor_screenshot.h
// kShotScale): their width/height attributes are the CSS size, half the pixels, so they show at the editor's own
// scale and stay sharp on high-DPI screens. og-image.png and card.png are exact-size 1x images and are not halved.
const SHOT_SCALE = 2;
const MD_IMAGE_RE = /!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g;

class SiteError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'SiteError';
    this.code = code;
  }
}

function replaceAll(text, placeholder, value) {
  return text.split(placeholder).join(value);
}

function fill(text, values, what) {
  let out = text;
  for (const [key, value] of Object.entries(values)) out = replaceAll(out, `{{${key}}}`, value);
  const left = [...new Set(out.match(PLACEHOLDER_RE) ?? [])];
  if (left.length > 0) throw new SiteError('E_SITE_PLACEHOLDER', `${what}: unreplaced ${left.join(' ')}`);
  return out;
}

function unescapeHtml(text) {
  return text.replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
}

/** The content of <meta name="@p name"> in @p html, unescaped; '' when absent. */
function metaContent(html, name) {
  const m = new RegExp(`<meta\\s+name="${name}"\\s+content="([^"]*)"`, 'i').exec(html);
  return m ? unescapeHtml(m[1]) : '';
}

function titleOf(html) {
  const m = /<title>([^<]*)<\/title>/i.exec(html);
  return m ? unescapeHtml(m[1]).trim() : '';
}

function newestDate(dates) {
  let best = '';
  for (const d of dates) if (d !== '' && (best === '' || Date.parse(d) > Date.parse(best))) best = d;
  return best;
}

function rel(root, ...parts) {
  return path.join(root, ...parts);
}

// --- discovery ---------------------------------------------------------------------------------------------------

/** The released plugins in directory order, with what each one's pages are built from. */
function discoverPlugins(deps) {
  const root = deps.root;
  let config;
  try {
    config = JSON.parse(deps.readFile(rel(root, 'docs', 'site.json')));
  } catch (err) {
    throw new SiteError('E_SITE_CONFIG', `docs/site.json: ${err.message}`);
  }
  const dirs = deps
    .readDir(rel(root, 'plugins'))
    .filter((d) => !d.startsWith('.') && deps.isFile(rel(root, 'plugins', d, 'version.json')))
    .filter((d) => deps.isFile(rel(root, 'plugins', d, 'docs', 'index.html')))
    .sort();
  const plugins = [];
  for (const dir of dirs) {
    const tag = (deps.gitTags(`${dir}/v*`)[0] ?? '').trim();
    if (tag === '') continue;
    const base = rel(root, 'plugins', dir);
    let meta;
    try {
      meta = JSON.parse(deps.readFile(path.join(base, 'version.json')));
    } catch (err) {
      throw new SiteError('E_SITE_VERSION_JSON', `plugins/${dir}/version.json: ${err.message}`);
    }
    for (const field of ['name', 'description', 'publisher', 'url']) {
      if (typeof meta[field] !== 'string' || meta[field] === '') {
        throw new SiteError('E_SITE_VERSION_JSON', `plugins/${dir}/version.json: "${field}" is not a non-empty string`);
      }
    }
    const url = `${common.SITE_ORIGIN}/${dir}/`;
    if (meta.url !== url) {
      throw new SiteError('E_SITE_VERSION_JSON', `plugins/${dir}/version.json: url ${meta.url} is not ${url}`);
    }
    const site = config.plugins?.[dir];
    if (!site || !config.groups?.includes(site.group) || typeof site.summary !== 'string') {
      throw new SiteError('E_SITE_CONFIG', `docs/site.json: no valid entry for plugins/${dir} (group, summary)`);
    }
    const imagesDir = path.join(base, 'images');
    const images = deps.isDir(imagesDir) ? deps.readDir(imagesDir).filter((f) => f.endsWith('.png')).sort() : [];
    plugins.push({
      dir,
      tag,
      version: tag.split('/v').pop(),
      name: meta.name,
      description: meta.description,
      publisher: meta.publisher,
      url,
      group: site.group,
      summary: site.summary,
      images,
      hasManual: deps.isFile(path.join(base, 'MANUAL.md')) && deps.isFile(path.join(base, 'docs', 'manual-template.html')),
      hasSignalFlow: deps.isFile(path.join(base, 'docs', `${dir}_signal_flow.dot`)),
      hasChangelog: deps.isFile(path.join(base, 'CHANGELOG.md')),
    });
  }
  return { config, plugins };
}

/** Site path, size and alt of a plugin's PNG in images/, or null when it has none by that name. */
function pluginImage(deps, plugin, file, alt) {
  if (!plugin.images.includes(file)) return null;
  const size = common.pngSize(deps.readBinary(rel(deps.root, 'plugins', plugin.dir, 'images', file)));
  if (size === null) throw new SiteError('E_SITE_IMAGE', `plugins/${plugin.dir}/images/${file} is not a PNG`);
  return { path: `/${plugin.dir}/images/${file}`, ...size, alt };
}

function ogImageOf(deps, plugin) {
  return pluginImage(deps, plugin, 'og-image.png', `${plugin.name} plugin editor`) ?? common.DEFAULT_OG_IMAGE;
}

function pageDates(deps, plugin) {
  const p = (...parts) => `plugins/${plugin.dir}/${parts.join('/')}`;
  const tagDate = deps.commitDate(plugin.tag, []);
  return {
    landing: newestDate([tagDate, deps.commitDate('HEAD', [p('docs', 'index.html'), p('docs', 'assets'), p('images'), p('version.json')])]),
    manual: deps.commitDate('HEAD', [p('MANUAL.md'), p('docs', 'manual-template.html'), p('images')]),
    signalFlow: deps.commitDate('HEAD', [p('docs', `${plugin.dir}_signal_flow.dot`), 'tools/generate-signal-flow.js']),
  };
}

// --- pages -------------------------------------------------------------------------------------------------------

function crumbs(plugin, leaf) {
  const list = [{ name: common.SITE_NAME, path: '/' }, { name: plugin.name, path: `/${plugin.dir}/` }];
  if (leaf) list.push(leaf);
  return list;
}

function otherPluginsHtml(plugins, config, self) {
  const others = plugins
    .filter((p) => p.dir !== self.dir)
    .sort((a, b) => config.groups.indexOf(a.group) - config.groups.indexOf(b.group) || a.name.localeCompare(b.name));
  if (others.length === 0) return '';
  const items = others.map(
    (p) =>
      `            <li><a href="/${p.dir}/"><strong>${common.escapeHtml(p.name)}</strong>` +
      `<span>${common.escapeHtml(p.description)}</span></a></li>`,
  );
  return [
    '        <section class="related">',
    '            <h2>More free plugins from Krate Audio</h2>',
    '            <ul class="related-list">',
    ...items,
    '            </ul>',
    '        </section>',
  ].join('\n');
}

function heroHtml(hero) {
  if (hero === null) return '';
  return [
    '        <figure class="hero">',
    `            <img src="images/${path.posix.basename(hero.path)}" width="${hero.width}" height="${hero.height}" ` +
      `alt="${common.escapeHtml(hero.alt)}" fetchpriority="high" decoding="async">`,
    '        </figure>',
  ].join('\n');
}

function renderLanding(deps, ctx, plugin) {
  const template = deps.readFile(rel(deps.root, 'plugins', plugin.dir, 'docs', 'index.html'));
  const heroFile = pluginImage(deps, plugin, 'hero.png', `${plugin.name} plugin editor`);
  const hero = heroFile && { ...heroFile, width: heroFile.width / SHOT_SCALE, height: heroFile.height / SHOT_SCALE };
  const og = ogImageOf(deps, plugin);
  const values = {
    VERSION: plugin.version,
    TAG: encodeURIComponent(plugin.tag),
    REPO: ctx.repo,
    NAME: common.escapeHtml(plugin.name),
    DESCRIPTION: common.escapeHtml(plugin.description),
    PUBLISHER: common.escapeHtml(plugin.publisher),
    URL: plugin.url,
    HERO: heroHtml(hero),
    OTHER_PLUGINS: otherPluginsHtml(ctx.plugins, ctx.config, plugin),
  };
  // The page's own description feeds its structured data, so it is read after the other values are filled.
  const draft = fill(template, { ...values, HEAD_COMMON: '' }, `plugins/${plugin.dir}/docs/index.html`);
  const software = {
    '@type': 'SoftwareApplication',
    '@id': `${plugin.url}#software`,
    name: plugin.name,
    description: metaContent(draft, 'description'),
    url: plugin.url,
    applicationCategory: 'MultimediaApplication',
    applicationSubCategory: 'Audio Plugin',
    operatingSystem: 'Windows, macOS, Linux',
    softwareVersion: plugin.version,
    downloadUrl: `https://github.com/${ctx.repo}/releases/tag/${encodeURIComponent(plugin.tag)}`,
    image: common.absUrl(og.path),
    isAccessibleForFree: true,
    license: 'https://opensource.org/licenses/MIT',
    offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
    author: { '@id': `${common.SITE_ORIGIN}/#organization` },
    publisher: { '@id': `${common.SITE_ORIGIN}/#organization` },
  };
  if (hero !== null) software.screenshot = common.absUrl(hero.path);
  if (plugin.hasChangelog) software.releaseNotes = `https://github.com/${ctx.repo}/blob/main/plugins/${plugin.dir}/CHANGELOG.md`;
  if (ctx.dates[plugin.dir].landing !== '') software.dateModified = ctx.dates[plugin.dir].landing;
  const head = common.headCommon({ ogImage: og, jsonLd: [software, common.breadcrumbList(crumbs(plugin))] });
  return { html: fill(template, { ...values, HEAD_COMMON: head }, `plugins/${plugin.dir}/docs/index.html`), hero };
}

/** Adds width / height (from the PNG) and lazy, async decoding to every <img> that lacks them. */
function addImageAttributes(html, sizeOf) {
  return html.replace(/<img\b([^>]*?)\s*\/?>/g, (whole, attrs) => {
    const src = /\bsrc="([^"]+)"/.exec(attrs);
    let out = attrs;
    if (src && !/\bwidth=/.test(out)) {
      const size = sizeOf(src[1]);
      if (size !== null) out += ` width="${size.width}" height="${size.height}"`;
    }
    if (!/\bloading=/.test(out)) out += ' loading="lazy"';
    if (!/\bdecoding=/.test(out)) out += ' decoding="async"';
    return `<img${out}>`;
  });
}

/** The manual's markdown: the first H1 dropped (the template carries the title), as docs.yml's sed did. */
function manualMarkdown(text) {
  const lines = text.split('\n');
  if (lines.length > 0 && /^# /.test(lines[0])) lines.shift();
  return lines.join('\n');
}

function manualImages(markdown) {
  return [...markdown.matchAll(MD_IMAGE_RE)].map((m) => ({ alt: m[1], src: m[2] }));
}

function renderManual(deps, ctx, plugin) {
  const base = rel(deps.root, 'plugins', plugin.dir);
  const markdown = manualMarkdown(deps.readFile(path.join(base, 'MANUAL.md')));
  const raw = deps.pandoc(markdown, path.join(base, 'docs', 'manual-template.html'));
  const url = `${plugin.url}manual.html`;
  const draft = raw.split('{{HEAD_COMMON}}').join('');
  const article = {
    '@type': 'TechArticle',
    headline: titleOf(draft).replace(/\s*\|\s*Krate Audio$/, ''),
    description: metaContent(draft, 'description'),
    url,
    inLanguage: 'en',
    image: common.absUrl(ogImageOf(deps, plugin).path),
    about: { '@id': `${plugin.url}#software` },
    author: { '@id': `${common.SITE_ORIGIN}/#organization` },
    publisher: { '@id': `${common.SITE_ORIGIN}/#organization` },
  };
  if (ctx.dates[plugin.dir].manual !== '') article.dateModified = ctx.dates[plugin.dir].manual;
  const head = common.headCommon({
    ogImage: ogImageOf(deps, plugin),
    jsonLd: [article, common.breadcrumbList(crumbs(plugin, { name: 'User Manual', path: `/${plugin.dir}/manual.html` }))],
  });
  const filled = fill(raw, { HEAD_COMMON: head }, `plugins/${plugin.dir} manual`);
  const sizeOf = (src) => {
    if (!src.startsWith('images/') || !plugin.images.includes(src.slice('images/'.length))) return null;
    const size = common.pngSize(deps.readBinary(path.join(base, ...src.split('/'))));
    return size && { width: Math.round(size.width / SHOT_SCALE), height: Math.round(size.height / SHOT_SCALE) };
  };
  return { html: addImageAttributes(filled, sizeOf), images: manualImages(markdown) };
}

async function renderSignalFlowPage(deps, ctx, plugin) {
  const dot = deps.readFile(rel(deps.root, 'plugins', plugin.dir, 'docs', `${plugin.dir}_signal_flow.dot`));
  const svg = await deps.renderDot(dot);
  const sitePath = `/${plugin.dir}/signal-flow.html`;
  const head = common.headCommon({
    ogImage: ogImageOf(deps, plugin),
    jsonLd: [common.breadcrumbList(crumbs(plugin, { name: 'Signal Flow', path: sitePath }))],
  });
  return signalFlow.renderSignalFlowPage({
    pluginName: plugin.name,
    description:
      `Interactive signal flow diagram of ${plugin.name}, the free ${plugin.description} VST3 plugin ` +
      'by Krate Audio, every stage from input to output.',
    canonical: common.absUrl(sitePath),
    headCommon: head,
    dotSource: dot,
    svg,
    hasManual: plugin.hasManual,
  });
}

function render404(deps, ctx) {
  const template = deps.readFile(rel(deps.root, 'docs', '404.html'));
  const links = ctx.plugins.map(
    (p) =>
      `                <li><a href="/${p.dir}/"><strong>${common.escapeHtml(p.name)}</strong>` +
      `<span>${common.escapeHtml(p.description)}</span></a></li>`,
  );
  return fill(template, { HEAD_COMMON: common.headCommon({ noindex: true }), PLUGIN_LINKS: links.join('\n') }, 'docs/404.html');
}

function renderSitemap(entries) {
  const urls = entries.map((e) => {
    const lines = ['  <url>', `    <loc>${common.escapeHtml(e.loc)}</loc>`];
    if (e.lastmod) lines.push(`    <lastmod>${e.lastmod}</lastmod>`);
    for (const img of e.images ?? []) {
      lines.push('    <image:image>', `      <image:loc>${common.escapeHtml(img)}</image:loc>`, '    </image:image>');
    }
    lines.push('  </url>');
    return lines.join('\n');
  });
  return [
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:image="http://www.google.com/schemas/sitemap-image/1.1">',
    ...urls,
    '</urlset>',
    '',
  ].join('\n');
}

// --- build -------------------------------------------------------------------------------------------------------

function copyTree(deps, from, to) {
  for (const entry of deps.readDir(from)) {
    const src = path.join(from, entry);
    if (deps.isDir(src)) copyTree(deps, src, path.join(to, entry));
    else deps.copyFile(src, path.join(to, entry));
  }
}

async function build(deps, opts) {
  const root = deps.root;
  const out = path.resolve(root, opts.out);
  const repo = deps.repo();
  if (typeof repo !== 'string' || repo === '') throw new SiteError('E_SITE_REPO', 'REPO is not set in the environment');
  const { config, plugins } = discoverPlugins(deps);
  const dates = Object.fromEntries(plugins.map((p) => [p.dir, pageDates(deps, p)]));
  const ctx = { repo, config, plugins, dates };
  const written = [];
  const write = (sitePath, content) => {
    deps.writeFile(path.join(out, ...sitePath.split('/').filter(Boolean)), content);
    written.push(sitePath);
  };

  const sitemap = [];
  for (const plugin of plugins) {
    const base = rel(root, 'plugins', plugin.dir);
    const d = dates[plugin.dir];

    const landing = renderLanding(deps, ctx, plugin);
    sitemap.push({ loc: plugin.url, lastmod: d.landing, images: landing.hero ? [common.absUrl(landing.hero.path)] : [] });
    write(`/${plugin.dir}/index.html`, landing.html);
    if (deps.isDir(path.join(base, 'docs', 'assets'))) copyTree(deps, path.join(base, 'docs', 'assets'), path.join(out, plugin.dir, 'assets'));
    for (const img of plugin.images) deps.copyFile(path.join(base, 'images', img), path.join(out, plugin.dir, 'images', img));

    if (plugin.hasManual) {
      const images = manualImages(deps.readFile(path.join(base, 'MANUAL.md')))
        .filter((i) => i.src.startsWith('images/') && plugin.images.includes(i.src.slice('images/'.length)))
        .map((i) => `${plugin.url}${i.src}`);
      sitemap.push({ loc: `${plugin.url}manual.html`, lastmod: d.manual, images: [...new Set(images)] });
      if (!opts.noManuals) write(`/${plugin.dir}/manual.html`, renderManual(deps, ctx, plugin).html);
    }

    if (plugin.hasSignalFlow) {
      sitemap.push({ loc: `${plugin.url}signal-flow.html`, lastmod: d.signalFlow });
      write(`/${plugin.dir}/signal-flow.html`, await renderSignalFlowPage(deps, ctx, plugin));
    }
  }

  // The root index: gen-root-index renders it from the same deps (plugin tags, the manager card, docs/site.json).
  const io = { stdout: { write: () => true }, stderr: { write: (s) => ((ctx.rootErr = (ctx.rootErr ?? '') + s), true) } };
  const rootDeps = { ...deps, writeFile: (p, text) => write('/index.html', text) };
  if (rootIndex.main(['--out', path.join(out, 'index.html')], io, rootDeps) !== 0) {
    throw new SiteError('E_SITE_ROOT_INDEX', (ctx.rootErr ?? '').trim());
  }
  const rootDate = newestDate([
    ...sitemap.map((e) => e.lastmod),
    deps.commitDate('HEAD', ['docs/index-template.html', 'docs/manager-card-template.html', 'docs/site.json']),
  ]);
  sitemap.unshift({ loc: `${common.SITE_ORIGIN}/`, lastmod: rootDate });

  write('/404.html', render404(deps, ctx));
  write('/sitemap.xml', renderSitemap(sitemap));
  for (const f of deps.readDir(rel(root, 'docs'))) {
    if (f === 'robots.txt' || f === 'favicon.ico' || /^google[0-9a-f]+\.html$/.test(f)) deps.copyFile(rel(root, 'docs', f), path.join(out, f));
  }
  copyTree(deps, rel(root, 'docs', 'assets'), path.join(out, 'assets'));
  return { out, written, plugins };
}

// --- CLI ---------------------------------------------------------------------------------------------------------

function parseArgs(argv) {
  const opts = { out: null, noManuals: false };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--no-manuals' && !opts.noManuals) opts.noManuals = true;
    else if (a === '--out' && opts.out === null && i + 1 < argv.length && argv[i + 1] !== '') opts.out = argv[(i += 1)];
    else return null;
  }
  return opts.out === null ? null : opts;
}

async function main(argv, io, deps) {
  const opts = parseArgs(argv);
  if (opts === null) {
    io.stderr.write(`${NAME}: E_USAGE: ${JSON.stringify(argv)}\n${USAGE}\n`);
    return 2;
  }
  try {
    const result = await build(deps, opts);
    const findings = seoLint.checkSite(result.out, { partial: opts.noManuals });
    for (const f of findings) io.stderr.write(`${NAME}: E_SITE_LINT: ${f}\n`);
    if (findings.length > 0) return 1;
    io.stdout.write(`${NAME}: ok, wrote ${result.written.length} pages for ${result.plugins.length} plugins to ${opts.out}\n`);
    return 0;
  } catch (err) {
    if (!(err instanceof SiteError)) throw err;
    io.stderr.write(`${NAME}: ${err.code}: ${err.message}\n`);
    return 1;
  }
}

/** Renders a .dot source to an SVG string with @viz-js/viz from tools/site/node_modules; null when it is not installed. */
async function vizRenderer(root) {
  let viz;
  try {
    // Resolved from tools/site, which docs.yml installs with npm ci.
    viz = await require(require.resolve('@viz-js/viz', { paths: [path.join(root, 'tools', 'site')] })).instance();
  } catch {
    return () => null;
  }
  return (dot) => viz.renderString(dot, { format: 'svg' });
}

function productionDeps(root = process.cwd()) {
  const git = (args) => childProcess.execFileSync('git', args, { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  let render = null;
  return {
    root,
    repo: () => process.env.REPO,
    gitTags: (pattern) =>
      git(['tag', '-l', pattern, '--sort=-v:refname'])
        .split('\n')
        .map((l) => l.trim())
        .filter((l) => l !== ''),
    commitDate: (rev, paths) => {
      try {
        return git(['log', '-1', '--format=%cI', rev, '--', ...paths]).trim();
      } catch {
        return '';
      }
    },
    readFile: (p) => fs.readFileSync(p, 'utf8'),
    readBinary: (p) => fs.readFileSync(p),
    readDir: (p) => fs.readdirSync(p),
    isFile: (p) => fs.statSync(p, { throwIfNoEntry: false })?.isFile() === true,
    isDir: (p) => fs.statSync(p, { throwIfNoEntry: false })?.isDirectory() === true,
    writeFile: (p, content) => {
      fs.mkdirSync(path.dirname(p), { recursive: true });
      fs.writeFileSync(p, content);
    },
    copyFile: (from, to) => {
      fs.mkdirSync(path.dirname(to), { recursive: true });
      fs.copyFileSync(from, to);
    },
    pandoc: (markdown, template) => {
      const r = childProcess.spawnSync(
        'pandoc',
        ['--from', 'markdown', '--to', 'html5', '--template', template, '--toc', '--toc-depth=3', '--no-highlight'],
        { input: markdown, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 },
      );
      if (r.status !== 0) throw new SiteError('E_SITE_PANDOC', `pandoc ${template}: ${r.error?.message ?? r.stderr}`);
      return r.stdout;
    },
    renderDot: async (dot) => {
      if (render === null) render = await vizRenderer(root);
      return render(dot);
    },
  };
}

/**
 * A pandoc stand-in for checks that run without pandoc (tools/lint-site-seo.js): $toc$ becomes an empty list and
 * $body$ one heading plus every image the markdown references, so the template's head and the image checks still run.
 */
function stubPandoc(readFile) {
  return (markdown, template) => {
    const images = manualImages(markdown).map((i) => `<p><img src="${common.escapeHtml(i.src)}" alt="${common.escapeHtml(i.alt)}" /></p>`);
    const body = ['<h2 id="manual">Manual</h2>', ...images].join('\n');
    return readFile(template).split('$toc$').join('<ul></ul>').split('$body$').join(body);
  };
}

module.exports = {
  SiteError,
  discoverPlugins,
  renderSitemap,
  addImageAttributes,
  manualMarkdown,
  manualImages,
  build,
  main,
  productionDeps,
  stubPandoc,
};

if (require.main === module) {
  main(process.argv.slice(2), process, productionDeps()).then((code) => {
    process.exitCode = code;
  });
}
