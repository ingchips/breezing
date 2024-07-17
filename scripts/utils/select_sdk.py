import sys, os
from dataclasses import dataclass
import json, platform

this_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
PATH_ZEP = os.path.abspath(os.path.join(this_dir, '../../'))
PATH_DTS = os.path.join(PATH_ZEP, 'dts/arm/ingchips')
PATH_SDK = ''

download_ini_template = """
[main]
family=ing916
timeout=10000

[options]
download=1
verify=0
redownload=1
set-entry=1
launch=1
batch=1
script=
protection.enabled=0
protection.unlock=0
UseScript=0
batch.current=0
batch.limit=-1
ResetReservedFlash=1
writeprotection.enabled=0

[uart]
Baud=460800
Port=COM3
Parity=
DataBits=8
StopBits=1
Timeout=10000

[bin-0]
Name=Burn Bin #1
Checked=1
FileName={platform_bin_file}
Address=33562624

[bin-1]
Name=Burn Bin #2
Checked=1
Address={app_flash_start}
FileName={zephyr_bin_file}
"""

@dataclass
class ChipSeries:
    bundle_name: str
    bundle_series: str
    dts_name: str
    flash_base: int
    flash_size: int
    ram_base: int
    ram_size: int

def prepare_values(series: ChipSeries) -> dict:
    r = {}
    with open(os.path.join(PATH_SDK, f'bundles/{series.bundle_name}/{series.bundle_series}/meta.json')) as f:
        meta = json.load(f)

    ram_start = meta['ram']['base'] + meta['ram']['size']
    ram_start = ((ram_start + 7) // 8) * 8
    r['{app_ram_start_hex}'] = f'{ram_start:08x}'
    r['{app_ram_size}'] = series.ram_base + series.ram_size - ram_start

    r['{app_flash_start}'] = meta['app']['base']
    r['{app_flash_start_hex}'] = f"{meta['app']['base']:08x}"
    r['{app_flash_size}']      = series.flash_base + series.flash_size - meta['app']['base']

    r['{platform_bin_file}'] = os.path.join(PATH_SDK, f'bundles/{series.bundle_name}/{series.bundle_series}/platform.bin')
    r['{zephyr_bin_file}']   = os.path.join(PATH_ZEP, f'build/zephyr/zephyr.bin')

    r['{ING_SDK_BASE}'] = PATH_SDK
    return r

def apply_values(source: str, mapping: dict) -> str:
    for k in mapping.keys():
        source = source.replace(k, str(mapping[k]))
    return source

def gen_dts(dts_fn: str, mapping: dict):
    with open(os.path.join(PATH_DTS, dts_fn + '.tmpl'), 'r') as f:
        template = f.read()
    template = apply_values(template, mapping)

    with open(os.path.join(PATH_DTS, dts_fn), 'w') as f:
        f.write(template)

def gen_soc_cmakelists(series: str, mapping: dict):
    fn = os.path.join(PATH_ZEP, 'soc/arm/' + series + '/CMakeLists.txt')
    with open(fn + '.tmpl', 'r') as f:
        template = f.read()
    template = apply_values(template, mapping)

    with open(fn, 'w') as f:
        f.write(template)

def gen_downloader_ini(mapping: dict):
    template = apply_values(download_ini_template, mapping)
    with open(os.path.join(PATH_ZEP, 'flash_download.ini'), 'w') as f:
        f.write(template)

all_series = [
    ChipSeries('noos_mini', 'ING9168xx', 'ing9168', 0x02000000, 512 * 1024, 0x20000000, 56 * 1024)
]

if __name__ == '__main__':

    if len(sys.argv) != 2:
        print(f"usage: python {sys.argv[0]} /path/to/ingchips/sdk")
        print(f"where `sdk` folder contains `bundles`, `examples`, etc.")
        exit(-1)

    PATH_SDK = sys.argv[1]

    for series in all_series:
        values = prepare_values(series)
        gen_dts(series.dts_name + '.dtsi', values)
        gen_soc_cmakelists(series.dts_name, values)

    gen_downloader_ini(values)