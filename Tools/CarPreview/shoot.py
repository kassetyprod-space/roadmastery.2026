import sys, subprocess, time, os
from playwright.sync_api import sync_playwright
here = os.path.dirname(os.path.abspath(__file__))
srv = subprocess.Popen([sys.executable, '-m', 'http.server', '8765'], cwd=here, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.8)
views = [l.split(' ', 1) for l in sys.argv[1:]]
try:
    with sync_playwright() as p:
        b = p.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
        pg = b.new_page(viewport={'width': 1400, 'height': 900})
        for name, h in views:
            pg.goto('about:blank')
            pg.goto(f'http://127.0.0.1:8765/viewer.html#{h}')
            pg.wait_for_function("document.title.length>0", timeout=120000)
            if pg.title() != 'done':
                print(name, pg.title())
            pg.screenshot(path=os.path.join(here, name + '.png'))
            print('ok', name)
        b.close()
finally:
    srv.terminate()
