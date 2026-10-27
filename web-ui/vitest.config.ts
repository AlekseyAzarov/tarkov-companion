import { defineConfig } from 'vitest/config';

export default defineConfig({
  define: {
    __BUNDLE_VERSION__: JSON.stringify('0.0.0-test'),
    __DEV__: 'false',
  },
  test: {
    environment: 'node',
    include: ['test/**/*.test.ts'],
  },
});
