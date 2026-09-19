import { Link } from 'react-router-dom';
import Logo from './Logo.jsx';

export default function Footer() {
  return (
    <footer className="bb-wrap bb-footer">
      <div className="bb-footer__grid">
        <div>
          <div className="bb-footer__head">Products</div>
          <div className="bb-footer__col">
            <Link to="/alpine">BitBit Alpine</Link>
            <Link to="/grains">BitBit Grains</Link>
          </div>
        </div>
        <div>
          <div className="bb-footer__head">Modules</div>
          <div className="bb-footer__col">
            <Link to="/alpine#artifact">Artifact</Link>
            <Link to="/alpine#modulation">Modulation</Link>
            <Link to="/alpine#delay">Delay</Link>
            <Link to="/alpine#reverb">Reverb</Link>
          </div>
        </div>
        <div>
          <div className="bb-footer__head">Resources</div>
          <div className="bb-footer__col">
            <Link to="/#ab-player">Demos</Link>
            <Link to="/#pricing">Pricing</Link>
            <a href="mailto:support@bitbit.audio">Support</a>
          </div>
        </div>
        <div>
          <div className="bb-footer__head">Company</div>
          <div className="bb-footer__col">
            <a href="#">About</a>
            <a href="#">Terms</a>
            <a href="#">Privacy</a>
          </div>
        </div>
      </div>
      <div className="bb-footer__bottom">
        <span>© 2026 BitBit Audio</span>
        <span style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
          <Logo width={24} height={18} stroke="#6c8288" strokeWidth={1.4} />
          BitBit Audio
        </span>
      </div>
    </footer>
  );
}
