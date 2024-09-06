# This file is part of WiFi Water Level S1 project.
#
# WiFi Water Level S1 is free software and hardware: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# Tested with python: Flask==3.0.2

import json

from flask import Flask, render_template, url_for, abort, request, redirect, current_app

app = Flask(__name__)


DEVICE_INFO = {}  # update this to persistent storage (cache, database, etc)


@app.route('/demo_server')
def demo_server():
    global DEVICE_INFO

    if not request.args.get('distance'):
        return json.dumps(DEVICE_INFO)

    WIFI_POOL_TIME_USER = 120  # Set here your preferred Wi-Fi pool time in seconds
    FIRMWARE_VERSION = int(request.headers.get('FW-Version', 0))  # Use device version to avoid call live update

    private_key = request.args.get('key')
    # validate private key here ...
    if private_key == "invalid":
        abort(404)

    distance = request.args.get('distance')
    voltage = request.args.get('voltage')
    rssi = int(request.headers.get('RSSI', 0))

    sensor_data = f"Sensor Info: >>> distance: {distance} cm | voltage: {voltage} v | rssi: {rssi} dBm | device firmware: {FIRMWARE_VERSION}"
    DEVICE_INFO['distance'] = f"{distance} cm"
    DEVICE_INFO['voltage'] = f"{float(voltage)/100.0} v"
    DEVICE_INFO['rssi'] = f"{rssi} dBm"
    DEVICE_INFO['firmware version'] = FIRMWARE_VERSION
    print(sensor_data)

    response = app.response_class(
        response='OK',
        status=200,
        mimetype='text/plain'
    )
    response.headers['fw-version'] = f"{FIRMWARE_VERSION}"
    response.headers['wpl'] = f"{WIFI_POOL_TIME_USER}"

    return response


if __name__ == '__main__':
    app.run(debug=True, port=80)
