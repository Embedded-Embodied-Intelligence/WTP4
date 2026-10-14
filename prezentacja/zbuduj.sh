#!/usr/bin/env bash
# Buduje materiały prezentacji: wykresy (Python), diagramy (Mermaid) i slajdy (Marp: HTML, PDF, PPTX).
# Wymagania: Python 3, Node.js (npx), przeglądarka Chrome / Chromium / Edge (renderowanie w tle).
# Użycie (z dowolnego katalogu):  ./prezentacja/zbuduj.sh
set -euo pipefail
cd "$(dirname "$0")"

# Przeglądarka używana przez Marp i Mermaid (można wskazać ręcznie: CHROME_PATH=/ścieżka ./zbuduj.sh)
if [[ -z "${CHROME_PATH:-}" ]]; then
  for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
           "/Applications/Chromium.app/Contents/MacOS/Chromium" \
           "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
           "$(command -v google-chrome || true)" "$(command -v chromium || true)" \
           "$(command -v chromium-browser || true)"; do
    if [[ -n "$c" && -x "$c" ]]; then CHROME_PATH="$c"; break; fi
  done
fi
: "${CHROME_PATH:?Nie znaleziono Chrome/Chromium/Edge – ustaw zmienną CHROME_PATH}"
export CHROME_PATH PUPPETEER_EXECUTABLE_PATH="$CHROME_PATH" PUPPETEER_SKIP_DOWNLOAD=1

echo "1/3 Wykresy z danych pomiarowych (Python)"
python3 wykresy.py

echo "2/3 Diagramy (Mermaid)"
printf '{"executablePath": "%s"}\n' "$CHROME_PATH" > .puppeteer.json
for f in diagramy/*.mmd; do
  npx -y -p @mermaid-js/mermaid-cli@12.0.0 mmdc -q -p .puppeteer.json -c diagramy/mermaid.json \
    -i "$f" -o "img/$(basename "${f%.mmd}").png" -s 3 -b white
done
rm -f .puppeteer.json

echo "3/3 Slajdy (Marp): HTML, PDF, PPTX"
# --html zezwala na znaczniki HTML w treści slajdów (układ kolumn); format wynika z opcji i rozszerzenia.
MARP=(npx -y @marp-team/marp-cli@4.5.1 slajdy.md --html --allow-local-files)
"${MARP[@]}" -o slajdy.html
"${MARP[@]}" --pdf -o slajdy.pdf
"${MARP[@]}" --pptx -o slajdy.pptx
echo "Gotowe: prezentacja/slajdy.html, slajdy.pdf, slajdy.pptx"
