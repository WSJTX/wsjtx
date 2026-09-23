/* Progressive enhancement: replace the static "latest release" labels with the
   current Latest Release from GitHub. If the API is unavailable (offline, rate
   limit, error), the static text in the HTML is left untouched.

   Also reveals a page's "Release Candidate" guide card, if present -- an
   <a data-rc-file="..." data-rc-version="X.Y.Z" hidden> -- whenever its
   declared version is newer than the current GA release. This keeps the card
   self-maintaining: it needs no update when GA finally catches up to the RC,
   only when a new RC is published (update data-rc-file/data-rc-version). */
(function () {
  var vEls = document.querySelectorAll(".js-latest-version");
  var dEls = document.querySelectorAll(".js-latest-date");
  var rcCard = document.querySelector("[data-rc-version]");
  if (!vEls.length && !dEls.length && !rcCard) return;

  function versionGreater(a, b) {
    var pa = a.split(".").map(Number);
    var pb = b.split(".").map(Number);
    var len = Math.max(pa.length, pb.length);
    for (var i = 0; i < len; i++) {
      var na = pa[i] || 0, nb = pb[i] || 0;
      if (na !== nb) return na > nb;
    }
    return false;
  }

  fetch("https://api.github.com/repos/WSJTX/wsjtx/releases/latest", {
    headers: { Accept: "application/vnd.github+json" }
  })
    .then(function (r) { return r.ok ? r.json() : null; })
    .then(function (rel) {
      if (!rel || !rel.tag_name) return;
      var num = String(rel.tag_name).replace(/^v/, "");
      var name = rel.name && /WSJT-X/i.test(rel.name) ? rel.name.trim() : "WSJT-X " + num;
      vEls.forEach(function (el) { el.textContent = name; });
      if (rel.published_at) {
        var d = new Date(rel.published_at);
        var s = d.toLocaleDateString("en-US", { day: "numeric", month: "short", year: "numeric" });
        dEls.forEach(function (el) { el.textContent = s; });
      }
      if (rcCard) {
        var rcVersion = rcCard.getAttribute("data-rc-version");
        var rcFile = rcCard.getAttribute("data-rc-file");
        if (rcVersion && rcFile && versionGreater(rcVersion, num)) {
          rcCard.href = rcFile;
          rcCard.hidden = false;
        }
      }
    })
    .catch(function () { /* keep the static fallback; RC card stays hidden */ });
})();
