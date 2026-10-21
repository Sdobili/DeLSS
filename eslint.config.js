import js from '@eslint/js';
import globals from 'globals';

export default [
  { ignores: ['node_modules/', 'build/', 'test-results/', 'web/src/generated.js', 'web/vendor/'] },
  js.configs.recommended,
  {
    files: ['web/**/*.js'],
    languageOptions: { globals: globals.browser },
  },
  {
    files: ['tools/**/*.mjs', 'tests/**/*.mjs', 'eslint.config.js'],
    languageOptions: { globals: { ...globals.node, window: 'readonly', document: 'readonly' } },
  },
  {
    rules: {
      'no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
      eqeqeq: 'error',
      'prefer-const': 'error',
      'no-var': 'error',
    },
  },
];
