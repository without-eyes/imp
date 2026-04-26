#!/usr/bin/env python3

import socket
import json
import time
import psutil
import numpy as np
from sklearn.ensemble import IsolationForest
import logging

# Налаштування логера для самого Python-скрипта
logging.basicConfig(level=logging.INFO, format='[%(asctime)s] [%(levelname)s] %(message)s')

SOCKET_PATH = "/tmp/imp_broker.sock"
MODULE_NAME = "ML_Anomaly"

def send_alert(level, message):
    """Відправляє JSON-повідомлення у C-Ядро I.M.P. через UNIX-сокет."""
    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(SOCKET_PATH)
        payload = {
            "source": MODULE_NAME,
            "level": level,
            "message": message
        }
        client.sendall(json.dumps(payload).encode('utf-8'))
        client.close()
    except Exception as e:
        logging.error(f"Не вдалося відправити IPC повідомлення: {e}")

def collect_metrics():
    """Збирає поточні метрики системи."""
    cpu = psutil.cpu_percent(interval=None)
    ram = psutil.virtual_memory().percent
    swap = psutil.swap_memory().percent
    # Можна додати метрики мережі та диска для ще більшої точності
    return [cpu, ram, swap]

def main():
    logging.info("Запуск ML модуля виявлення аномалій...")
    
    # ---------------------------------------------------------
    # ФАЗА 1: Збір даних та Навчання (Training)
    # ---------------------------------------------------------
    logging.info("Збір базових метрик (baseline) для навчання моделі (15 секунд)...")
    training_data = []
    
    # Збираємо дані нормального стану системи
    for _ in range(15):
        training_data.append(collect_metrics())
        time.sleep(1)
        
    # Створюємо та навчаємо модель
    # contamination=0.05 означає, що ми очікуємо до 5% аномалій у майбутньому
    clf = IsolationForest(n_estimators=100, contamination=0.05, random_state=42)
    clf.fit(training_data)
    logging.info("Модель Isolation Forest успішно навчена!")
    
    # Сповіщаємо Ядро, що ML-модуль став до роботи
    send_alert("INFO", "Модель IsolationForest навчена. Починаю моніторинг.")

    # ---------------------------------------------------------
    # ФАЗА 2: Моніторинг та Інференс (Inference)
    # ---------------------------------------------------------
    consecutive_anomalies = 0
    
    while True:
        time.sleep(2) # Аналізуємо систему кожні 2 секунди
        current_metrics = collect_metrics()
        
        # Передбачення: 1 - норма, -1 - аномалія
        prediction = clf.predict([current_metrics])[0]
        # decision_function дає "оцінку аномальності" (від'ємна = погано)
        score = clf.decision_function([current_metrics])[0]
        
        if prediction == -1:
            consecutive_anomalies += 1
            # Щоб уникнути спаму від випадкових стрибків (наприклад, відкриття браузера),
            # підіймаємо тривогу тільки якщо аномалія триває кілька тактів поспіль
            if consecutive_anomalies >= 2:
                cpu, ram, swap = current_metrics
                msg = f"Аномалію виявлено! Оцінка: {score:.2f} | CPU: {cpu}% RAM: {ram}%"
                logging.warning(msg)
                send_alert("CRITICAL", msg)
        else:
            consecutive_anomalies = 0 # Скидаємо лічильник, якщо все нормально

if __name__ == "__main__":
    main()