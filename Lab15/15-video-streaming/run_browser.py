import os
import sys
from selenium import webdriver
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.firefox.service import Service
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from time import sleep

ip = sys.argv[1]
run_time = int(sys.argv[2])
    
# url
url = 'http://{}/index.html'.format(ip)

try:
    # initialize browser driver
    service = Service('/usr/bin/geckodriver')
    options=Options()
    # options.add_argument('--autoplay-policy=no-user-gesture-required')
    driver=webdriver.Firefox(service=service, options=options)
    
    # run browser
    driver.get(url)
    driver.set_page_load_timeout(10)
    print('page content got, playing now...')
    
    t_min = int(run_time / 60)
    t_sec = run_time % 60
    play_time_str = ''
    play_time_str += str(t_min) if t_min >= 10 else ('0' + str(t_min))
    play_time_str += ':'
    play_time_str += str(t_sec) if t_sec >= 10 else ('0' + str(t_sec))

    try:
        WebDriverWait(driver, 2000, 0.5).until(EC.text_to_be_present_in_element((By.ID, 'videoTime'), play_time_str))
    except Exception as e:
        print(driver.find_element(By.ID, 'iconPlayPause').get_attribute("class"))
    
    driver.quit()

    print('done')
    
except Exception as e:
    try:
        driver.quit()
    except:
        pass

    print(e)
