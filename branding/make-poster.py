# Venetium branding (as MDM RT's and 7-Zip RT's make-poster.py): renders the README poster (branding/poster/poster.html,
# 1920x800 CSS) to a PNG at 1.5x (2880x1200) with headless Chrome and a throwaway profile.
#   python -I make-poster.py <out.png> <profile folder>
#   e.g. python -I make-poster.py D:\repo\venetium\branding\poster-v1.4.png F:\work\poster-chrome
# poster.html takes its three device screenshots from poster-v1.3.png, so keep that file.
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CHROME = os.environ.get('RT_CHROME', r'C:\Program Files\Google\Chrome\Application\chrome.exe')
out, prof = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
html = os.path.join(HERE, 'poster', 'poster.html')
if os.path.exists(prof):
    sys.exit(prof + ' exists already (use a new folder)')
if os.path.exists(out):
    sys.exit(out + ' exists already')
args = [CHROME, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1.5',
        '--window-size=1920,800', '--user-data-dir=' + prof, '--no-first-run', '--no-default-browser-check',
        '--screenshot=' + out, 'file:///' + html.replace('\\', '/')]
p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    p.wait(90)
except subprocess.TimeoutExpired:
    p.kill()   # only the Chrome this script started (its own process, by handle)
    sys.exit('chrome did not finish in 90 s')
if not os.path.exists(out):
    sys.exit('no ' + out)
w, h = struct.unpack('>II', open(out, 'rb').read(24)[16:24])
print('%s  %dx%d  %d KB' % (out, w, h, os.path.getsize(out) // 1024))
