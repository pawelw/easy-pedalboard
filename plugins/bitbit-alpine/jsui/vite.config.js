import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Port 3002 - Wah is 3000 and Delay 3001, so all three dev servers can run at
// once while iterating on more than one face in a session.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3002,
    strictPort: true,
  },
  build: {
    outDir: "dist",
  },
});
