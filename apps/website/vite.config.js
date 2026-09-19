import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: { port: 3200 },
  // public/assets holds the product screenshots; keep the bundles out of that folder.
  build: { assetsDir: 'static' },
});
