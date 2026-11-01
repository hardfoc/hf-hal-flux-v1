// Centralized Doxygen Version Selector
(function() {
  const versions = [];

  function createVersionSelector() {
    const selector = document.createElement('div');
    selector.id = 'doxygen-version-selector';
    selector.style.cssText = 'position: fixed; top: 10px; right: 10px; ' +
      'z-index: 1000; background: #fff; border: 1px solid #ccc; ' +
      'border-radius: 4px; padding: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);';

    const select = document.createElement('select');
    select.style.cssText = 'margin-right: 8px; padding: 4px;';

    const currentVersion = window.location.pathname.split('/')[2] || 'latest';

    versions.forEach(version => {
      const option = document.createElement('option');
      option.value = version;
      option.textContent = version;
      if (version === currentVersion) option.selected = true;
      select.appendChild(option);
    });

    select.addEventListener('change', function() {
      const selectedVersion = this.value;
      if (selectedVersion && selectedVersion !== currentVersion) {
        const currentPath = window.location.pathname;
        const newPath = currentPath.replace(/\/[^\/]+\//, `/${selectedVersion}/`);
        window.location.href = newPath;
      }
    });

    const label = document.createElement('label');
    label.textContent = 'API Version: ';
    label.style.cssText = 'margin-right: 8px; font-weight: bold;';

    selector.appendChild(label);
    selector.appendChild(select);

    document.body.appendChild(selector);
  }

  // Load versions dynamically from centralized location
  fetch('/doxs/versions.json')
    .then(response => response.json())
    .then(data => {
      versions.push(...data.versions);
      createVersionSelector();
    })
    .catch(() => {
      // Fallback: create selector with current version only
      versions.push(currentVersion);
      createVersionSelector();
    });
})();
