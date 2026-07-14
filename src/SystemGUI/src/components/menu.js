import { El } from "@frameable/el";
import store from "./store";

class Menu extends El {
  _setPage(page) {
    store.state.page = page;
    if (window.location.hash !== `#${page}`) {
      window.history.replaceState(null, "", `#${page}`);
    }
    window.dispatchEvent(new CustomEvent("openace:navigate", { detail: { page } }));
    this.$update();
  }

  _saveBr() {
    store.storeInBRModuleData().then(() => {
      store.init();
    });
  }

  render(html) {
    const navItem = (page, label) => html`
      <li>
        <a
          href="#${page}"
          data-page="${page}"
          aria-current="${store.state.page === page ? "page" : "false"}"
          onclick=${() => this._setPage(page)}
        >${label}</a>
      </li>
    `;

    return html`
      <header class="app-header">
        <a class="app-brand" href="#session" onclick=${() => this._setPage("session")}>
          <span class="app-logo" aria-hidden="true">OA</span>
          <span class="app-brand-copy">
            <strong>OpenAce</strong>
            <small>${store.state.aircraftId || "Aviation connectivity"}</small>
          </span>
        </a>

        <nav class="app-nav" aria-label="Primary navigation">
          <ul>
            ${navItem("session", "Aircraft")}
            ${navItem("modules", "Modules")}
            ${navItem("status", "Status")}
            ${navItem("actions", "Actions")}
          </ul>
        </nav>

        <div class="header-actions">
          <span
            class="connection-status"
            data-connected="${store.state.connected}"
            title="${store.state.connected ? "Connected" : "Disconnected"}"
          >${store.state.connected ? "Connected" : "Disconnected"}</span>
          <button
            type="button"
            class="flash-button ${store.state.configModified ? "is-modified" : ""}"
            onclick=${() => this._saveBr()}
          >Save</button>
        </div>
      </header>
    `;
  }
}

customElements.define("main-menu", Menu);
