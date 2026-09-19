import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Port 3001, not Wah's 3000 - both dev servers can run at once without a
// clash while iterating on more than one pedal's face in the same session.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3001,
    strictPort: true,
  },
  build: {
    outDir: "dist",
  },
});
