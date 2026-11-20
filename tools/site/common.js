// tools/site/common.js
// =============================================================================
// Shared by the krateaudio.com generators (tools/gen-site.js, tools/gen-root-index.js) and the SEO lint
// (tools/lint-site-seo.js): the site origin, HTML escaping, PNG dimensions, and the <head> block every page carries.
//
// WHY ONE HEAD BLOCK. The nine plugin pages were hand-copied from each other and drifted: four lost their favicon, four
// shipped an unreplaced {{URL}} canonical, none had an og:image. Every tag a page shares with the others (icons,
// robots, social card, font, structured data) is now written here, once, and each template keeps only what is its own
// (title, description, canonical, og:title / og:description / og:url).
// =============================================================================
'use strict';

const SITE_ORIGIN = 'https://krateaudio.com';
const SITE_NAME = 'Krate Audio';
const GITHUB_ORG_URL = 'https://github.com/rolandzwaga';
const DEFAULT_OG_IMAGE = { path: '/assets/og-image.png', width: 1200, height: 630, alt: 'Krate Audio: free open-source VST3 plugins' };
const LOGO_PATH = '/assets/logo-512.png';

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/** Width and height from a PNG's IHDR chunk, or null when @p buf is not a PNG. */
function pngSize(buf) {
  if (!Buffer.isBuffer(buf) || buf.length < 24) return null;
  if (buf.readUInt32BE(0) !== 0x89504e47 || buf.toString('ascii', 12, 16) !== 'IHDR') return null;
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) };
}

/** The absolute URL of a site path ("/", "/iterum/", "/iterum/manual.html"). */
function absUrl(sitePath) {
  return SITE_ORIGIN + sitePath;
}

/** A <script type="application/ld+json"> block; "<" is escaped so no string can close the script element. */
function jsonLdScript(data, indent) {
  const pad = ' '.repeat(indent);
  const body = JSON.stringify(data, null, 2).replace(/</g, '\\u003c');
  return [`${pad}<script type="application/ld+json">`, ...body.split('\n').map((l) => pad + l), `${pad}</script>`].join('\n');
}

function organization() {
  return {
    '@type': 'Organization',
    '@id': `${SITE_ORIGIN}/#organization`,
    name: SITE_NAME,
    url: `${SITE_ORIGIN}/`,
    logo: { '@type': 'ImageObject', url: absUrl(LOGO_PATH), width: 512, height: 512 },
    sameAs: [GITHUB_ORG_URL],
  };
}

/** BreadcrumbList over [{ name, path }], root first. */
function breadcrumbList(crumbs) {
  return {
    '@type': 'BreadcrumbList',
    itemListElement: crumbs.map((c, i) => ({ '@type': 'ListItem', position: i + 1, name: c.name, item: absUrl(c.path) })),
  };
}

/**
 * The shared part of a page's <head>, indented 4 spaces like the templates.
 *   ogImage   { path, width, height, alt }  (site path; DEFAULT_OG_IMAGE when omitted)
 *   jsonLd    the page's own structured-data nodes; written as one @graph with the Organization
 *   noindex   true for pages that must stay out of the index (404)
 */
function headCommon({ ogImage = DEFAULT_OG_IMAGE, jsonLd = [], noindex = false } = {}) {
  const img = ogImage;
  const lines = [
    `<meta name="robots" content="${noindex ? 'noindex, follow' : 'index, follow, max-image-preview:large'}">`,
    '<meta name="theme-color" content="#0a0a0f">',
    '<link rel="icon" href="/favicon.ico" sizes="32x32">',
    '<link rel="icon" type="image/svg+xml" href="/assets/favicon.svg">',
    '<link rel="apple-touch-icon" href="/assets/apple-touch-icon.png">',
    '<link rel="preload" href="/assets/fonts/inter-latin.woff2" as="font" type="font/woff2" crossorigin>',
    '<link rel="stylesheet" href="/assets/site.css">',
    `<meta property="og:site_name" content="${SITE_NAME}">`,
    '<meta property="og:locale" content="en_US">',
    `<meta property="og:image" content="${absUrl(img.path)}">`,
    `<meta property="og:image:width" content="${img.width}">`,
    `<meta property="og:image:height" content="${img.height}">`,
    `<meta property="og:image:alt" content="${escapeHtml(img.alt)}">`,
    '<meta name="twitter:card" content="summary_large_image">',
    `<meta name="twitter:image" content="${absUrl(img.path)}">`,
  ].map((l) => `    ${l}`);
  const graph = noindex ? [] : [organization(), ...jsonLd];
  if (graph.length > 0) lines.push(jsonLdScript({ '@context': 'https://schema.org', '@graph': graph }, 4));
  return lines.join('\n');
}

module.exports = {
  SITE_ORIGIN,
  SITE_NAME,
  DEFAULT_OG_IMAGE,
  LOGO_PATH,
  escapeHtml,
  pngSize,
  absUrl,
  breadcrumbList,
  headCommon,
};
