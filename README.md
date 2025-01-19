# Setup [Libre Le Potato - AML-S905X-CC (64-bit)](https://www.amazon.com/Libre-Computer-AML-S905X-CC-Potato-64-bit/dp/B074P6BNGZ/ref=sr_1_2?crid=23XUBHWHW39CB&keywords=Libre+Le+Potato+-+AML-S905X-CC&qid=1680817463&sprefix=libre+le+potato+-+aml-s905x-cc%2Caps%2C106&sr=8-2)
- This mini motherboard shares the same form size with Raspberry-pi 3B but with a faster cpu and much more memory.
- The official website provide either ubuntu image and raspbian image
  - raspbian seems less bulky and works pretty well with all hw components as well as Home Assistant Core
  - Highly recommended to use a faster micro sd-card or use eMMC Module (IO is pretty slow)

# Before...
- update hostnames in these files and reboot
  - `/etc/hostname`
  - `/etc/hosts`
  - Or `hostnamectl set-hostname new-hostname` and confirm with `hostnamectl`

## Wifi `Cudy AC650` and `Asusconumer AC600 + BT`

### Build & Install the Device Driver for rtl8821cu
~~~
git clone https://github.com/morrownr/8821cu-20210118
cd 8821cu-20210118
./ARM64_RPI.sh
sudo ./install-driver.sh
# reboot accordingly
~~~

- install bluetooth packages
  - `apt-get install -y bluez bluez-tools blueman`
    - `systemctl status bluetooth.service`
- Why the user can see any bluetooth devices?
  - the user might not have access to bluetooth (need to be in the `bluetooth` group)
    - `gpasswd -a homeassistant bluetooth`
    - `gpasswd -a pi bluetooth`
  - You should be able use `bluetoothctl` to list and scan
    - `lsusb` should show you the usb bt receiver and you should see bt firmware loading in `dmesg`

### Configure wifi connection
- Reference: https://wiki.archlinux.org/title/wpa_supplicant
-  Scan for SSIDs
  `iwlist wlan0 scans`
- Create a file named `/etc/wpa_supplicant/wpa_supplicant.conf` with following content
  ~~~
  country=US
  update_config=1
  ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev

  network={
    ssid="Fishy NNetwork"
    psk=".........."
    proto=RSN
    key_mgmt=WPA-PSK
  }

  network={
    ssid="FishyLake"
    psk=".........."
    proto=RSN
    key_mgmt=WPA-PSK
  }
  ~~~
- `wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf`
- To restart wlan0: `ifdown wlan0 && ifup wlan0`
- Check status: `interface wlan0`
 
## ASUS BT500 Firmware
- The firmware for this bluetooth dongle very likely is missing on raspbian
- run `lsusb` and you would see the ASUS BT500 device (but not working)
- run `dmesg` and you will see firemware missing for that bluetooth dongle

### How to get the missing firmware?
- ASUS does not have firmware for linux but we are luckily enough to get it from mpow
  - Direct download (might change): https://cdn.shopify.com/s/files/1/0249/2891/1420/files/20201202_BH456A_driverforLinux-1_0929.7z?v=1664445632
  - or https://www.xmpow.com/pages/download and look for BH456A Bluetooth USB Adapter
- Unzip the download file and look for `rtl876b_*` and copy them to `/lib/firmware/rtl_bt`
  - you would need to rename them if system is looking for `rtl876bu` instead (`b` and `bu` are the same)
  ~~~
  cd /lib/firmware/rtl_bt
  sudo ln -s rtl8761b_config.bin rtl8761bu_config.bin
  sudo ln -s rtl8761b_fw.bin rtl8761bu_fw.bin
  ~~~
  
# Home Assistant Core via systemd

## Install Home Assistant Core
- Follow [the official doc](https://www.home-assistant.io/installation/linux#install-home-assistant-core)
- Additional packages
  - `apt-get install -y ffmpeg` for Ring Integration

### Systemd unit
- `cat /etc/systemd/system/home-assistant\@homeassistant.service`
~~~
[Unit]
Description=Home Assistant
After=network-online.target

[Service]
Type=simple
User=%i
WorkingDirectory=/home/%i/.homeassistant
ExecStart=/srv/homeassistant/bin/hass -c "/home/%i/.homeassistant"
RestartForceExitStatus=100
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
~~~

## Custom Integration

### MQTT and Mosquitto
- `apt-get install -y mosquitto mosquitto-client`
- `systemctl status mosquitto`
#### Secure Mosquitto (MQTT Broker)
- Make the server only accessible locally with password
- add `/etc/mosquitto/conf.d/default.conf`
  ~~~
  per_listener_settings true

  listener 1883 localhost
  allow_anonymous true

  listener 8883 0.0.0.0
  allow_anonymous false

  password_file /etc/mosquitto/passwd
  ~~~
- add `/etc/mosquitto/passwd`
  ~~~
  fishylake:<secret>
  ~~~
  - run `mosquitto_passwd -U passwd && chgrp mosquitto /etc/mosquitto/passwd && chmod 640 /etc/mosquitto/passwd`
  - run `systemctl restart mosquitto` to reload the config
- Test mqtt
  - http://www.steves-internet-guide.com/mosquitto_pub-sub-clients/
  - receive all topics
    `mosquitto_sub -h localhost -v -t \#`
  - send a message
    `mosquitto_pub -h localhost -t testing/123 -m {\"status\":\"OFF\"}`

### Radio SDR rtl-433
- ```
  apt-get install libtool libusb-1.0-0-dev librtlsdr-dev rtl-sdr build-essential autoconf cmake pkg-config
  ```
- https://www.switchdoc.com/2020/11/tutorial-raspberrypi-433mhz-weatherrack2-2/
  ```
  cd ~
  git clone https://github.com/merbanan/rtl_433.git
  cd rtl_433
  mkdir build
  cd build
  cmake ..
  make
  sudo make install
  ```
- connect rtl_433 to mqtt broker
  - https://github.com/jacklty/homeassistant/blob/master/etc/systemd/system/rtl_433.service
  - https://github.com/jacklty/homeassistant/blob/master/etc/rtl_433/service.conf

### Midea Dehumidifier
- You would need to have the devices first added to the Midea Air App via a cloud account (not with Apple ID or facebook)
  - User: admin@compulty.com
  - Password: <Hint: first date of internet and plain old password>
  - You can always share them later to different Midea Accounts/Users
- As with Core, you need to install it manually
  ~~~
  # As the `homeassistant` user
  sudo -u homeassistant -H -s
  cd ~
  # Obtain the custom integration for Midea
  git clone https://github.com/nbogojevic/homeassistant-midea-air-appliances-lan
  mkdir -p ~/.homeassistant/custom_components
  # Copy the custom integration to Home Assistant Config folder
  cp -R homeassistant-midea-air-appliances-lan/custom_components/midea_dehumidifier_lan ~/.homeassistant/custom_components
  ~~~
- Restart `Home Assistant` to pick up new integration

# Misc.

## piVPN
- 
  ~~~
  curl -L https://install.pivpn.io | bash
  ~~~
- Why OpenVPN over WireGuard?
  Router integration is still lacking for WireGuard, so we should stick with OpenVPN for now.
- iptables rules can be found in `/etc/iptables/rules.v4`
  - make sure the postrouting rules are using the correct inet otherwise clients can't access the internet (or local network)

## ddclient and google domain
- 
  ~~~
  apt-get install ddclient
  ~~~
- Obtain the credentail for specific dynamic dns record (at the very bottom on domains.google.com)
  ~~~
  # /etc/ddclient.conf

  protocol=googledomains \
  use=web, web=https://domains.google.com/checkip \
  login=... \
  password=... \
  fishylake.compulty.com
  ~~~
### How to manually update DDNS?
```
# Obtain the corresponding credential of the DDNS record.
curl "https://${LOGIN}:${PASSWORD}@domains.google.com/nic/update?hostname=fishylake.compulty.com&myip=$(curl ipconfig.io)"
```
## Let's Encrypted
- 
  ~~~
  sudo apt install certbot
  sudo certbot certonly --manual --preferred-challenges dns -d fishylake.compulty.com
  # follow the instruction to update the TXT record to complete the verification
  ~~~
# Homebridge
https://github.com/homebridge/homebridge/wiki/Install-Homebridge-on-Debian-or-Ubuntu-Linux
~~~
curl -sSfL https://repo.homebridge.io/KEY.gpg | sudo gpg --dearmor | sudo tee /usr/share/keyrings/homebridge.gpg  > /dev/null
echo "deb [signed-by=/usr/share/keyrings/homebridge.gpg] https://repo.homebridge.io stable main" | sudo tee /etc/apt/sources.list.d/homebridge.list > /dev/null
sudo apt-get update
sudo apt-get install homebridge
systemctl status homebrige
~~~

# Key Error
- If `apt-get update` errors out about `The following signatures were invalid: EXPKEYSIG 2E5FB7FC58C58FFB deb.libre.computer <contact+deb@libre.computer>`
- Follow this instruct to update the base image
  - https://hub.libre.computer/t/signatures-were-invalid-expkeysig-2e5fb7fc58c58ffb/4166/2 
  ~~~
  wget https://deb.libre.computer/repo/pool/main/libr/libretech-keyring/libretech-keyring_2024.05.19_all.deb
  sudo dpkg -i libretech-keyring_2024.05.19_all.deb
  sudo apt update
  ~~~
