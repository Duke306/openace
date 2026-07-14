import { El } from "@frameable/el";
import store from "./store";
import "./aircraftsession";
import "./aircraftconfig";

const MAX_AIRCRAFT = 10;
class Session extends El {
  created() {
    this.state = this.$observable({
      showHelp: false,
      editAircraft: false,
      aircraft: "",
      deleteAircraftDlg: false,
    });
  }

  render(html) {
    return this.state.editAircraft ? this._editAircraft(html) : this._selection(html);
  }

  _editAircraft(html) {
    return html` <aircraft-config key="config" close=${() => (this.state.editAircraft = false)} selected="${this.state.aircraft}"> </aircraft-config> `;
  }

  _aircraftUpdated(aircraftId) {
    store.setDefaultAirCraftId(aircraftId);
  }

  _add() {
    this.state.aircraft = "";
    this.state.editAircraft = true;
  }
  _edit() {
    this.state.aircraft = store.state.aircraftId;
    this.state.editAircraft = true;
  }

  _deleteAircraft() {
    return store
      .deleteAircraft(store.state.aircraftId)
      .catch((e) => {
        this.state.data.length = 0;
      })
      .finally(() => {
        this.state.deleteAircraftDlg = false;
      });
  }

  _selection(html) {
    var deleteDlg = this.state.deleteAircraftDlg ? this._deleteAircraftDlg(html) : "";
    return html`
      ${deleteDlg}
      <aircraft-session key="session" selected="${store.state.aircraftId}" changed=${this._aircraftUpdated}></aircraft-session>

      <div class="form-actions form-actions--3">
        <input
          type="submit"
          class="secondary"
          value="Add ${store.state.numberOfAircrafts >= MAX_AIRCRAFT ? `(Max ${MAX_AIRCRAFT})` : ""}"
          onclick=${this._add}
          ${store.state.numberOfAircrafts >= MAX_AIRCRAFT ? "disabled" : ""}
        />
        <input
          type="submit"
          class="secondary"
          value="Remove"
          onclick=${() => (this.state.deleteAircraftDlg = true)}
          ${store.state.numberOfAircrafts < 2 ? "disabled" : ""}
        />
        <input type="submit" value="Modify" onclick=${this._edit} />
      </div>
    `;
  }

  _deleteAircraftDlg(html) {
    return html` <div class="app-modal" role="dialog" aria-modal="true" aria-labelledby="delete-aircraft-title">
      <div class="app-modal__surface">
        <article>
          <header>
            <h4 id="delete-aircraft-title">Delete '${store.state.aircraftId}'?</h4>
          </header>
          <div class="app-modal__body">
            <p>Removal of <b>${store.state.aircraftId}</b> cannot be undone.<br />Are you sure you want to delete ${store.state.aircraftId} aircraft?</p>
          </div>
          <footer class="app-modal__actions">
            <button type="button" class="danger" onclick=${this._deleteAircraft}>Delete</button>
            <button type="button" class="secondary" onclick=${() => (this.state.deleteAircraftDlg = false)}>Cancel</button>
          </footer>
        </article>
      </div>
    </div>`;
  }
}

customElements.define("gatas-session", Session);
