import { Activity, Ticket, Wallet } from 'lucide-react';

import './styles.css';

const statusItems = [
  { icon: Activity, label: 'Engine', value: 'scaffolded' },
  { icon: Wallet, label: 'Wallet', value: 'ledger planned' },
  { icon: Ticket, label: 'Tickets', value: 'ownership planned' },
];

export function App() {
  return (
    <main className="app-shell">
      <section className="workspace">
        <header className="topbar">
          <div>
            <p className="eyebrow">TicketX</p>
            <h1>Exchange Console</h1>
          </div>
          <span className="status-pill">MVP scaffold</span>
        </header>

        <div className="status-grid">
          {statusItems.map((item) => {
            const Icon = item.icon;
            return (
              <article className="status-card" key={item.label}>
                <Icon aria-hidden="true" size={20} />
                <div>
                  <p>{item.label}</p>
                  <strong>{item.value}</strong>
                </div>
              </article>
            );
          })}
        </div>
      </section>
    </main>
  );
}
