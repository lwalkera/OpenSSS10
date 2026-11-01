// OpenBeken's /app page loads <webapp root>startup.js; load the app itself
// (gzipped on the device, see tools/webapp.py)
var s = document.createElement('script');
s.src = window.root + 'app.js' + (window.appSuffix === undefined ? '.gz' : window.appSuffix);
document.head.appendChild(s);
