// Extract and validate JS from generateReceivePage
const fs = require('fs');
const content = fs.readFileSync('src/network/RequestHandler.cpp', 'utf8');

// Find the generateReceivePage function
const start = content.indexOf('QByteArray RequestHandler::generateReceivePage');
const end = content.indexOf('\n}', content.indexOf('</script>', start));

const func = content.substring(start, end);

// Extract C++ string literals and concatenate them
const matches = [...func.matchAll(/"([^"\\]*(\\.[^"\\]*)*)"/g)];
let html = matches.map(m => m[1]).join('');

// Extract JS between <script> and </script>
const scriptStart = html.indexOf('<script>') + 8;
const scriptEnd = html.indexOf('</script>');
const js = html.substring(scriptStart, scriptEnd);

console.log('=== JS Length:', js.length);
console.log('=== First 200 chars:', js.substring(0, 200));
console.log('=== Last 200 chars:', js.substring(js.length - 200));

// Try to parse the JS
try {
    new Function(js);
    console.log('=== JS PARSE: OK');
} catch(e) {
    console.log('=== JS PARSE ERROR:', e.message);
}
