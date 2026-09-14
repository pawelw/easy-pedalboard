import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { PedalUIProvider } from "@synthpeak/pedal-ui";
import App from "./App.jsx";

createRoot(document.getElementById("root")).render(
  <StrictMode>
    {/* TEMPORARY: light-theme preview - restore theme="onyx" when done. */}
    <PedalUIProvider>
      <App />
    </PedalUIProvider>
  </StrictMode>,
);
