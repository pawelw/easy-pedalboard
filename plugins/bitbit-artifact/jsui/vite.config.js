import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Port 3003 - Wah is 3000, Delay 3001, Alpine 3002, so every dev server can run
// at once while iterating on more than one face in a session.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3003,
    strictPort: true,
  },
  build: {
    outDir: "dist",
  },
});
