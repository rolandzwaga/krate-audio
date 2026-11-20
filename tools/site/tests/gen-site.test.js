'use strict';

// tools/gen-site.js over a fixture repository written to a temporary directory: two released plugins (alpha with a
// manual, signal flow and screenshots; beta with a landing page only) and one untagged plugin (gamma). The repository's
// own docs/ (root template, 404 page, assets) is copied in, so the shared head and root index are the real ones.
// git and pandoc are injected: tags and commit dates come from tables, pandoc is the stand-in lint-site-seo uses.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const gen = require(path.join(REPO_ROOT, 'tools', 'gen-site.js'));

const TAGS = { 'alpha/v*': ['alpha/v1.2.0', 'alpha/v1.1.0'], 'beta/v*': ['beta/v0.3.0'], 'gamma/v*': [], 'manager/v*': [] };
const DATES = {
  'alpha/v1.2.0': '2026-10-01T10:00:00+00:00',
  'beta/v0.3.0': '2026-09-01T10:00:00+00:00',
};

/** A PNG header (signature + IHDR) of the given size: all tools/site/common.js pngSize() reads. */
function png(width, height) {
  const b = Buffer.alloc(33);
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(b, 0);
  b.writeUInt32BE(13, 8);
  b.write('IHDR', 12, 'ascii');
  b.writeUInt32BE(width, 16);
  b.writeUInt32BE(height, 20);
  return b;
}

function landingTemplate(dir, desc) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>{{NAME}} – Free Fixture VST3 Plugin ${dir} | Krate Audio</title>
    <meta name="description" content="${desc}">
    <link rel="canonical" href="{{URL}}">
    <meta property="og:title" content="{{NAME}}">
    <meta property="og:description" content="${desc}">
    <meta property="og:type" content="website">
    <meta property="og:url" content="{{URL}}">
{{HEAD_COMMON}}
    <link rel="stylesheet" href="assets/style.css">
</head>
<body>
    <div class="container">
        <header>
            <h1>{{NAME}}</h1>
            <p class="tagline">{{DESCRIPTION}} v{{VERSION}}</p>
        </header>

{{HERO}}
        <a href="https://github.com/{{REPO}}/releases/download/{{TAG}}/x.exe">Download</a>
        <section class="signal-flow">
{{DOC_LINKS}}
        </section>
{{OTHER_PLUGINS}}

        <footer><p>{{PUBLISHER}}</p></footer>
    </div>
</body>
</html>
`;
}

function manualTemplate(name, dir) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
    <title>${name} User Manual – Fixture Plugin | Krate Audio</title>
    <meta name="description" content="User manual for ${name}, a fixture plugin of the gen-site tests: every section, every image.">
    <link rel="canonical" href="https://krateaudio.com/${dir}/manual.html">
    <meta property="og:title" content="${name} User Manual">
    <meta property="og:description" content="User manual for ${name}.">
    <meta property="og:type" content="article">
    <meta property="og:url" content="https://krateaudio.com/${dir}/manual.html">
{{HEAD_COMMON}}
</head>
<body>
    <h1>${name} — User Manual</h1>
    $toc$
    $body$
</body>
</html>
`;
}

function write(root, rel, content) {
  const p = path.join(root, ...rel.split('/'));
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, content);
}

/** The fixture repository; @p edit(root) may change it before the build. */
function fixture(edit) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'gen-site-test-'));
  fs.cpSync(path.join(REPO_ROOT, 'docs'), path.join(root, 'docs'), { recursive: true });
  write(
    root,
    'docs/site.json',
    JSON.stringify({
      groups: ['Instruments', 'Effects'],
      plugins: {
        alpha: { group: 'Instruments', summary: 'Alpha is the fixture instrument with a manual.' },
        beta: { group: 'Effects', summary: 'Beta is the fixture effect & has no hero image.' },
        gamma: { group: 'Effects', summary: 'Gamma is never released.' },
      },
    }),
  );
  const meta = (name, description) =>
    JSON.stringify({ version: '9.9.9', name, description, publisher: 'Krate Audio', url: `https://krateaudio.com/${name.toLowerCase()}/` });
  write(root, 'plugins/alpha/version.json', meta('Alpha', 'Fixture Instrument'));
  write(root, 'plugins/alpha/docs/index.html', landingTemplate('alpha', 'Alpha is a free open-source fixture instrument used by the gen-site tests only.'));
  write(root, 'plugins/alpha/docs/assets/style.css', 'body{}\n.signal-flow-link {\n    display: inline-block;\n}\n');
  write(root, 'plugins/alpha/docs/manual-template.html', manualTemplate('Alpha', 'alpha'));
  write(root, 'plugins/alpha/MANUAL.md', '# Alpha\n\n## Section\n\n![The main page](images/editor-main.png)\n');
  write(root, 'plugins/alpha/docs/alpha_signal_flow.dot', 'digraph { a -> b }');
  write(root, 'plugins/alpha/CHANGELOG.md', '# Changelog');
  write(root, 'plugins/alpha/images/hero.png', png(1920, 1200));
  write(root, 'plugins/alpha/images/og-image.png', png(1200, 630));
  write(root, 'plugins/alpha/images/card.png', png(600, 315));
  write(root, 'plugins/alpha/images/editor-main.png', png(1920, 1200));
  write(root, 'plugins/beta/version.json', meta('Beta', 'Fixture & Effect'));
  write(root, 'plugins/beta/docs/index.html', landingTemplate('beta', 'Beta is a free open-source fixture effect used by the gen-site tests and nothing else.'));
  write(root, 'plugins/beta/docs/assets/style.css', '.signal-flow-link { display: inline-block; }\n');
  write(root, 'plugins/beta/docs/manual-template.html', manualTemplate('Beta', 'beta'));
  write(root, 'plugins/beta/MANUAL.md', '# Beta\n\n## Section\n\nNo images.\n');
  write(root, 'plugins/beta/docs/beta_signal_flow.dot', 'digraph { c -> d }');
  write(root, 'plugins/gamma/version.json', meta('Gamma', 'Unreleased'));
  write(root, 'plugins/gamma/docs/index.html', landingTemplate('gamma', 'Gamma is an unreleased fixture plugin; no page of it may ever be written.'));
  if (edit) edit(root);
  return root;
}

function deps(root, overrides = {}) {
  const prod = gen.productionDeps(root);
  const calls = { pandoc: [] };
  const d = {
    ...prod,
    repo: () => 'o/r',
    gitTags: (pattern) => [...(TAGS[pattern] ?? [])],
    commitDate: (rev, paths) => (paths.length === 0 ? DATES[rev] ?? '' : paths.some((p) => p.includes('MANUAL')) ? '2026-08-01T00:00:00+00:00' : '2026-07-01T00:00:00+00:00'),
    pandoc: (markdown, template) => {
      calls.pandoc.push(markdown);
      return gen.stubPandoc(prod.readFile)(markdown, template);
    },
    renderDot: async () => '<svg><g><title>a</title><text>Stage A</text></g></svg>',
    ...overrides,
  };
  return { d, calls };
}

async function run(root, argv, overrides) {
  const { d, calls } = deps(root, overrides);
  let stdout = '';
  let stderr = '';
  const io = { stdout: { write: (s) => ((stdout += s), true) }, stderr: { write: (s) => ((stderr += s), true) } };
  const code = await gen.main(argv, io, d);
  return { code, stdout, stderr, calls };
}

function read(dir, rel) {
  return fs.readFileSync(path.join(dir, ...rel.split('/')), 'utf8');
}

function jsonLd(html) {
  const m = /<script type="application\/ld\+json">([\s\S]*?)<\/script>/.exec(html);
  return JSON.parse(m[1])['@graph'];
}

test('gen-site builds a lint-clean site from the plugin sources', async (t) => {
  const root = fixture();
  const out = path.join(root, '_site');
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const r = await run(root, ['--out', out]);
  assert.equal(r.code, 0, r.stderr);

  await t.test('every landing placeholder is filled; the canonical is the version.json url (the {{URL}} regression)', () => {
    const html = read(out, 'alpha/index.html');
    assert.equal(/\{\{[A-Z_]+\}\}/.test(html), false);
    assert.ok(html.includes('<link rel="canonical" href="https://krateaudio.com/alpha/">'));
    assert.ok(html.includes('<meta property="og:url" content="https://krateaudio.com/alpha/">'));
    assert.ok(html.includes('https://github.com/o/r/releases/download/alpha%2Fv1.2.0/x.exe'), 'newest tag, encoded');
    assert.ok(html.includes('Fixture Instrument v1.2.0'));
    assert.ok(read(out, 'beta/index.html').includes('Fixture &amp; Effect'), 'version.json text is HTML-escaped');
  });

  await t.test('the landing head carries the social card, icons and the SoftwareApplication data', () => {
    const html = read(out, 'alpha/index.html');
    assert.ok(html.includes('<meta property="og:image" content="https://krateaudio.com/alpha/images/og-image.png">'));
    assert.ok(html.includes('<meta name="twitter:card" content="summary_large_image">'));
    assert.ok(html.includes('<link rel="icon" href="/favicon.ico" sizes="32x32">'));
    const graph = jsonLd(html);
    const app = graph.find((n) => n['@type'] === 'SoftwareApplication');
    assert.equal(app.softwareVersion, '1.2.0');
    assert.equal(app.description, 'Alpha is a free open-source fixture instrument used by the gen-site tests only.');
    assert.equal(app.screenshot, 'https://krateaudio.com/alpha/images/hero.png');
    assert.equal(app.downloadUrl, 'https://github.com/o/r/releases/tag/alpha%2Fv1.2.0');
    assert.equal(app.dateModified, '2026-10-01T10:00:00+00:00', 'the tag date is newer than the sources');
    assert.equal(app.releaseNotes, 'https://github.com/o/r/blob/main/plugins/alpha/CHANGELOG.md');
    const crumbs = graph.find((n) => n['@type'] === 'BreadcrumbList').itemListElement.map((i) => i.item);
    assert.deepEqual(crumbs, ['https://krateaudio.com/', 'https://krateaudio.com/alpha/']);
    const beta = jsonLd(read(out, 'beta/index.html')).find((n) => n['@type'] === 'SoftwareApplication');
    assert.equal(beta.screenshot, undefined, 'no hero.png, no screenshot');
    assert.ok(read(out, 'beta/index.html').includes('content="https://krateaudio.com/assets/og-image.png"'), 'fallback card');
  });

  await t.test('the hero image has its PNG size; the other released plugins are linked, never itself or gamma', () => {
    const html = read(out, 'alpha/index.html');
    // 2x screenshots declare their CSS size, half the pixels.
    assert.ok(html.includes('<img src="images/hero.png" width="960" height="600" alt="Alpha plugin editor" fetchpriority="high"'));
    assert.ok(html.includes('<a href="/beta/"><strong>Beta</strong>'));
    assert.equal(html.includes('<a href="/alpha/">'), false);
    assert.equal(html.includes('/gamma/'), false);
    assert.equal(read(out, 'beta/index.html').includes('<figure class="hero">'), false);
  });

  await t.test('the documentation buttons are generated on every landing page, manual first, both the button class', () => {
    for (const dir of ['alpha', 'beta']) {
      const html = read(out, `${dir}/index.html`);
      assert.ok(html.includes('<a href="manual.html" class="signal-flow-link">User Manual &rarr;</a>'), dir);
      assert.ok(html.includes('<a href="signal-flow.html" class="signal-flow-link">Signal Flow Diagram &rarr;</a>'), dir);
      assert.ok(html.indexOf('href="manual.html"') < html.indexOf('href="signal-flow.html"'), `${dir}: manual first`);
    }
  });

  await t.test('an untagged plugin gets no page and no sitemap entry', () => {
    assert.equal(fs.existsSync(path.join(out, 'gamma')), false);
    assert.equal(read(out, 'sitemap.xml').includes('/gamma/'), false);
  });

  await t.test('the manual: pandoc gets MANUAL.md without its H1, images get size and lazy loading', () => {
    assert.equal(r.calls.pandoc.length, 2);
    assert.equal(r.calls.pandoc[0].startsWith('# Alpha'), false);
    const html = read(out, 'alpha/manual.html');
    assert.ok(html.includes('<img src="images/editor-main.png" alt="The main page" width="960" height="600" loading="lazy" decoding="async">'));
    const article = jsonLd(html).find((n) => n['@type'] === 'TechArticle');
    assert.equal(article.headline, 'Alpha User Manual – Fixture Plugin');
    assert.deepEqual(article.about, { '@id': 'https://krateaudio.com/alpha/#software' });
  });

  await t.test('the signal-flow page ships the pre-rendered SVG and no CDN renderer', () => {
    const html = read(out, 'alpha/signal-flow.html');
    assert.ok(html.includes('<text>Stage A</text>'));
    assert.equal(html.includes('cdn.jsdelivr.net'), false);
    assert.ok(html.includes('<title>Alpha Signal Flow Diagram | Krate Audio</title>'));
    assert.ok(html.includes('<a href="manual.html">user manual</a>'));
  });

  await t.test('the sitemap lists every page with its source date and images', () => {
    const xml = read(out, 'sitemap.xml');
    const locs = [...xml.matchAll(/<loc>([^<]*)<\/loc>/g)].map((m) => m[1]);
    assert.deepEqual(locs, [
      'https://krateaudio.com/',
      'https://krateaudio.com/alpha/',
      'https://krateaudio.com/alpha/manual.html',
      'https://krateaudio.com/alpha/signal-flow.html',
      'https://krateaudio.com/beta/',
      'https://krateaudio.com/beta/manual.html',
      'https://krateaudio.com/beta/signal-flow.html',
    ]);
    assert.ok(xml.includes('<loc>https://krateaudio.com/alpha/manual.html</loc>\n    <lastmod>2026-08-01T00:00:00+00:00</lastmod>'));
    assert.ok(xml.includes('<image:loc>https://krateaudio.com/alpha/images/hero.png</image:loc>'));
    assert.ok(xml.includes('<image:loc>https://krateaudio.com/alpha/images/editor-main.png</image:loc>'));
    assert.ok(xml.includes('<loc>https://krateaudio.com/</loc>\n    <lastmod>2026-10-01T10:00:00+00:00</lastmod>'), 'root: newest date');
    assert.equal(xml.includes('<priority>'), false, 'Google ignores priority');
  });

  await t.test('the root index groups the cards and the 404 page is noindex', () => {
    const html = read(out, 'index.html');
    assert.ok(html.indexOf('<h2 class="group-heading">Instruments</h2>') < html.indexOf('<h3>Alpha</h3>'));
    assert.ok(html.indexOf('<h3>Alpha</h3>') < html.indexOf('<h2 class="group-heading">Effects</h2>'));
    assert.ok(html.includes('<img src="alpha/images/card.png" width="600" height="315"'));
    assert.ok(html.includes('Beta is the fixture effect &amp; has no hero image.'));
    assert.ok(html.includes('<span class="released">Released <time datetime="2026-10-01">1 Oct 2026</time></span>'));
    assert.ok(html.includes('<span class="released">Released <time datetime="2026-09-01">1 Sep 2026</time></span>'));
    const notFound = read(out, '404.html');
    assert.ok(notFound.includes('<meta name="robots" content="noindex, follow">'));
    assert.ok(notFound.includes('<a href="/alpha/">'));
  });

  await t.test('shared files are copied to the root', () => {
    for (const f of ['favicon.ico', 'robots.txt', 'assets/site.css', 'assets/fonts/inter-latin.woff2', 'alpha/assets/style.css']) {
      assert.ok(fs.existsSync(path.join(out, ...f.split('/'))), f);
    }
  });
});

test('gen-site refuses what it cannot build correctly', async (t) => {
  const cases = [
    ['a placeholder the generator does not know', (root) => fs.appendFileSync(path.join(root, 'plugins/beta/docs/index.html'), '{{NOPE}}'), 'E_SITE_PLACEHOLDER'],
    ['a version.json url that is not the page address', (root) => write(root, 'plugins/beta/version.json', JSON.stringify({ name: 'Beta', description: 'd', publisher: 'p', url: 'https://example.com/' })), 'E_SITE_VERSION_JSON'],
    ['a released plugin missing from docs/site.json', (root) => write(root, 'docs/site.json', JSON.stringify({ groups: ['Effects'], plugins: {} })), 'E_SITE_CONFIG'],
    ['a page the SEO lint rejects (title over 60 characters)', (root) => {
      const p = path.join(root, 'plugins/beta/docs/index.html');
      fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('Free Fixture VST3 Plugin beta', 'Free Fixture VST3 Plugin beta with a title far too long'));
    }, 'E_SITE_LINT'],
    ['a landing page without {{DOC_LINKS}}', (root) => {
      const p = path.join(root, 'plugins/alpha/docs/index.html');
      fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('{{DOC_LINKS}}', ''));
    }, 'E_SITE_DOCS'],
    ['a landing page that hand-links the manual (the Gradus bare-text link)', (root) => {
      const p = path.join(root, 'plugins/alpha/docs/index.html');
      fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('{{DOC_LINKS}}', '{{DOC_LINKS}}\n<a href="manual.html">User Manual</a>'));
    }, 'E_SITE_DOCS'],
    ['a landing page that hand-links the signal flow', (root) => {
      const p = path.join(root, 'plugins/alpha/docs/index.html');
      fs.writeFileSync(p, fs.readFileSync(p, 'utf8').replace('{{DOC_LINKS}}', "{{DOC_LINKS}}\n<a href='signal-flow.html'>Diagram</a>"));
    }, 'E_SITE_DOCS'],
    ['a stylesheet without the button rule', (root) => write(root, 'plugins/alpha/docs/assets/style.css', 'body{}'), 'E_SITE_DOCS'],
    ['a released plugin without a manual (Iterum and Disrumpo shipped without one)', (root) => fs.rmSync(path.join(root, 'plugins/beta/MANUAL.md')), 'E_SITE_DOCS'],
    ['a released plugin without a manual template', (root) => fs.rmSync(path.join(root, 'plugins/beta/docs/manual-template.html')), 'E_SITE_DOCS'],
    ['a released plugin without a signal-flow source', (root) => fs.rmSync(path.join(root, 'plugins/beta/docs/beta_signal_flow.dot')), 'E_SITE_DOCS'],
  ];
  for (const [what, edit, code] of cases) {
    await t.test(what, async () => {
      const root = fixture(edit);
      try {
        const r = await run(root, ['--out', path.join(root, '_site')]);
        assert.equal(r.code, 1, r.stderr);
        assert.match(r.stderr, new RegExp(`^gen-site: ${code}: `, 'm'));
      } finally {
        fs.rmSync(root, { recursive: true, force: true });
      }
    });
  }

  await t.test('a malformed command line is exit 2 with the usage line', async () => {
    for (const argv of [[], ['--out'], ['--bogus'], ['--out', 'a', '--out', 'b'], ['--plugin', 'x', '--out', 'a']]) {
      const r = await run(REPO_ROOT, argv);
      assert.equal(r.code, 2, JSON.stringify(argv));
      assert.ok(r.stderr.includes('usage: node tools/gen-site.js --out <dir> [--no-manuals]'));
    }
  });

  await t.test('--no-manuals writes no manual but still lists it in the sitemap', async () => {
    const root = fixture();
    try {
      const out = path.join(root, '_site');
      const r = await run(root, ['--out', out, '--no-manuals']);
      assert.equal(r.code, 0, r.stderr);
      assert.equal(r.calls.pandoc.length, 0);
      assert.equal(fs.existsSync(path.join(out, 'alpha', 'manual.html')), false);
      assert.ok(read(out, 'sitemap.xml').includes('<loc>https://krateaudio.com/alpha/manual.html</loc>'));
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
});
