import { NavLink, Link } from 'react-router-dom';
import Logo from './Logo.jsx';

export default function Nav() {
  return (
    <nav className="bb-nav">
      <Link to="/" className="bb-nav__brand">
        <Logo size={46} />
        <span>BitBit Audio</span>
      </Link>
      <div className="bb-nav__links">
        <NavLink to="/alpine" className={({ isActive }) => (isActive ? 'is-active' : undefined)}>
          Alpine
        </NavLink>
        <NavLink to="/grains" className={({ isActive }) => (isActive ? 'is-active' : undefined)}>
          Grains
        </NavLink>
      </div>
      <Link to="/#pricing" className="bb-nav__buy">
        Buy
      </Link>
    </nav>
  );
}
