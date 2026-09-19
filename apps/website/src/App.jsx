import { useEffect } from 'react';
import { Routes, Route, useLocation } from 'react-router-dom';
import Nav from './components/Nav.jsx';
import Footer from './components/Footer.jsx';
import Home from './pages/Home.jsx';
import Alpine from './pages/Alpine.jsx';
import Grains from './pages/Grains.jsx';

/** react-router does not scroll for us: reset on navigation, honour #anchors. */
function ScrollManager() {
  const { pathname, hash } = useLocation();

  useEffect(() => {
    if (!hash) {
      window.scrollTo(0, 0);
      return;
    }
    const target = document.getElementById(hash.slice(1));
    if (target) target.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }, [pathname, hash]);

  return null;
}

export default function App() {
  return (
    <div className="bb-shell">
      <ScrollManager />
      <Nav />
      <Routes>
        <Route path="/" element={<Home />} />
        <Route path="/alpine" element={<Alpine />} />
        <Route path="/grains" element={<Grains />} />
        <Route path="*" element={<Home />} />
      </Routes>
      <Footer />
    </div>
  );
}
