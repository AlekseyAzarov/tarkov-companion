// Bundles the web flow into three static files (index.html, app.js, app.css).
//
//   node build.mjs                  -> dist/
//   node build.mjs --outdir <dir>   -> <dir> (used by Gradle, see BuildWebUiTask)
//   node build.mjs --serve          -> http://localhost:8000 with a mock bridge
import * as esbuild from 'esbuild';
import { copyFile, mkdir, readFile, stat } from 'node:fs/promises';
import { gzipSync } from 'node:zlib';
import path from 'node:path';
// Writes src/generated/contract.ts before anything imports it.
import '../scripts/generate-web-contract.mjs';

const commandLineArguments = process.argv.slice(2);
const serve = commandLineArguments.includes('--serve');
const outputDirectoryFlagIndex = commandLineArguments.indexOf('--outdir');
const outputDirectory = path.resolve(
  outputDirectoryFlagIndex >= 0 ? commandLineArguments[outputDirectoryFlagIndex + 1] : 'dist',
);
const DEVELOPMENT_SERVER_PORT = 8000;

// Gzipped JS budget. The bundle ships inside every host APK.
const JAVASCRIPT_GZIP_BUDGET_BYTES = 25 * 1024;

const packageJson = JSON.parse(await readFile('package.json', 'utf8'));

/** @type {esbuild.BuildOptions} */
const buildOptions = {
  entryPoints: ['src/main.tsx'],
  bundle: true,
  outdir: outputDirectory,
  entryNames: 'app',
  format: 'iife',
  // Android System WebView updates via Play, but old OEM builds linger.
  target: ['chrome80'],
  jsx: 'automatic',
  jsxImportSource: 'preact',
  minify: !serve,
  sourcemap: serve ? 'inline' : false,
  legalComments: 'none',
  define: {
    __BUNDLE_VERSION__: JSON.stringify(packageJson.version),
    __DEV__: JSON.stringify(serve),
  },
  logLevel: 'warning',
};

await mkdir(outputDirectory, { recursive: true });
await copyFile('src/index.html', path.join(outputDirectory, 'index.html'));

if (serve) {
  const context = await esbuild.context(buildOptions);
  await context.watch();
  const { port } = await context.serve({ servedir: outputDirectory, port: DEVELOPMENT_SERVER_PORT });
  console.log(`dev server: http://localhost:${port}/index.html (mock bridge)`);
} else {
  await esbuild.build(buildOptions);

  let overBudget = false;
  for (const file of ['app.js', 'app.css', 'index.html']) {
    const fullPath = path.join(outputDirectory, file);
    const uncompressedBytes = (await stat(fullPath)).size;
    const gzipBytes = gzipSync(await readFile(fullPath)).length;
    console.log(`${file.padEnd(11)} ${String(uncompressedBytes).padStart(7)} B  (${gzipBytes} B gzip)`);

    if (file === 'app.js' && gzipBytes > JAVASCRIPT_GZIP_BUDGET_BYTES) {
      console.error(`app.js exceeds the ${JAVASCRIPT_GZIP_BUDGET_BYTES} B gzip budget`);
      overBudget = true;
    }
  }

  if (overBudget) {
    process.exit(1);
  }
}
