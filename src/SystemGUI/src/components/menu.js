import { El } from "@frameable/el";
import store from "./store";
import darkLogoUrl from "url:../img/generated/gatas-dark.webp";

class Menu extends El {
  _setPage(page) {
    store.state.page = page;
    if (window.location.hash !== `#${page}`) {
      window.history.replaceState(null, "", `#${page}`);
    }
    window.dispatchEvent(new CustomEvent("gatas:navigate", { detail: { page } }));
    this.$update();
  }

  _saveBr() {
    if (!store.state.configModified || store.state.configurationEditorOpen) {
      return;
    }

    store.storeInBRModuleData().then(() => {
      store.init();
    });
  }

  render(html) {
    const saveDisabled = !store.state.configModified || store.state.configurationEditorOpen;
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
        <a class="app-brand" href="#session" aria-label="GATAS home" onclick=${() => this._setPage("session")}>
          <picture class="app-brand-picture">
            <img src="${darkLogoUrl}" alt="GATAS" width="320" height="80" />
          </picture>
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
            title="${store.state.configurationEditorOpen ? "Finish editing before storing to flash" : "Store configuration to flash"}"
            onclick=${() => this._saveBr()}
            ${saveDisabled ? "disabled" : ""}
          >Flash</button>
        </div>
      </header>
    `;
  }
}

customElements.define("main-menu", Menu);
