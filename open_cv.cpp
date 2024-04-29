#include <iostream>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <sqlite3.h>
#include <gtest/gtest.h>

using namespace cv;

// Połączenie z bazą danych SQLite
sqlite3* connect_to_database() {
    sqlite3* db;
    int rc = sqlite3_open("pszczoly_db.sqlite", &db);

    if (rc) {
        throw std::runtime_error("Błąd podczas połączenia z bazą danych SQLite: " + std::string(sqlite3_errmsg(db)));
    }

    std::cout << "Połączono z bazą danych SQLite." << std::endl;
    return db;
}

// Funkcja do tworzenia tabel w bazie danych SQLite
void create_tables(sqlite3* db) {
    char* error_message = nullptr;

    // Tworzenie tabeli dla lokalizacji uli pszczół
    const char* create_table_ul_pszczole = "CREATE TABLE IF NOT EXISTS ul_pszczole (" \
                                            "id INTEGER PRIMARY KEY AUTOINCREMENT," \
                                            "nazwa TEXT," \
                                            "lokalizacja BLOB," \
                                            "liczba_pszczol INTEGER," \
                                            "data_umieszczenia DATE);";

    // Wykonanie poleceń SQL
    int rc = sqlite3_exec(db, create_table_ul_pszczole, nullptr, 0, &error_message);

    if (rc != SQLITE_OK) {
        std::string error = "Błąd podczas tworzenia tabeli ul_pszczole: " + std::string(error_message);
        sqlite3_free(error_message);
        throw std::runtime_error(error);
    }

    std::cout << "Tabela ul_pszczole została utworzona." << std::endl;
}

// Zapisz informacje do bazy danych
void save_to_database(sqlite3* db, std::vector<std::vector<Point>>& contours_yellow) {
    char* error_message = nullptr;

    // Przygotowanie zapytania INSERT
    const char* insert_query = "INSERT INTO ul_pszczole (nazwa, lokalizacja, liczba_pszczol, data_umieszczenia) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insert_query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string error = "Błąd podczas przygotowywania zapytania INSERT: " + std::string(sqlite3_errmsg(db));
        throw std::runtime_error(error);
    }

    // Wstaw informacje dla każdego obszaru pszczół
    for (size_t i = 0; i < contours_yellow.size(); ++i) {
        // Pobierz lokalizację obszaru
        Point center;
        float radius;
        minEnclosingCircle(contours_yellow[i], center, radius);
        std::string location = "(" + std::to_string(center.x) + ", " + std::to_string(center.y) + ")";

        // Wstaw informacje do zapytania
        std::string name = "Obszar pszczół " + std::to_string(i + 1);
        int bee_count = contours_yellow[i].size(); // liczba pikseli obszaru to liczba pszczół
        std::string date = "2024-04-29"; // przykładowa data umieszczenia

        // Związanie wartości zapytania
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, location.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, bee_count);
        sqlite3_bind_text(stmt, 4, date.c_str(), -1, SQLITE_STATIC);

        // Wykonaj zapytanie
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string error = "Błąd podczas wykonania zapytania INSERT: " + std::string(sqlite3_errmsg(db));
            throw std::runtime_error(error);
        }

        // Zresetuj stan zapytania
        sqlite3_reset(stmt);
    }

    // Finalizacja zapytania
    sqlite3_finalize(stmt);
}

// Funkcja do przetwarzania obrazów i detekcji pszczół
void process_and_detect_bees(const std::string& imagePath, sqlite3* db) {
    // Inicjalizacja logowania wyników do pliku
    std::ofstream logFile("log.txt");
    if (!logFile.is_open()) {
        throw std::runtime_error("Błąd: Nie można otworzyć pliku log.txt");
    }

    try {
        // Wczytaj obraz
        Mat image = imread(imagePath);

        // Sprawdź poprawność wczytania obrazu
        if (image.empty()) {
            throw std::runtime_error("Błąd: Nie można wczytać obrazu.");
        }

        // Konwertuj obraz z BGR do HSV
        Mat hsv;
        cvtColor(image, hsv, COLOR_BGR2HSV);

        // Sprawdź poprawność konwersji kolorów
        if (hsv.empty()) {
            throw std::runtime_error("Błąd: Konwersja kolorów nie powiodła się.");
        }

        // Zdefiniuj zakres kolorów żółtych w przestrzeni HSV
        Scalar lower_yellow(20, 100, 100);
        Scalar upper_yellow(30, 255, 255);

        // Stwórz maskę dla kolorów żółtych
        Mat yellow_mask;
        inRange(hsv, lower_yellow, upper_yellow, yellow_mask);

        // Zdefiniuj zakres kolorów czarnych w przestrzeni HSV
        Scalar lower_black(0, 0, 0);
        Scalar upper_black(180, 255, 30);

        // Stwórz maskę dla kolorów czarnych
        Mat black_mask;
        inRange(hsv, lower_black, upper_black, black_mask);

        // Wykryj kontury na masce
        std::vector<std::vector<Point>> contours_yellow, contours_black;
        findContours(yellow_mask, contours_yellow, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
        findContours(black_mask, contours_black, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        // Sprawdź, czy wykryto jakieś kontury (pszczół)
        if (contours_yellow.empty() || contours_black.empty()) {
            std::cout << "Nie znaleziono pszczoł." << std::endl;
            return;
        }

        std::cout << "Znaleziono pszczoły!" << std::endl;

        // Zapisz informacje do bazy danych
        save_to_database(db, contours_yellow);

        // Przejdź przez kontury i narysuj prostokąty wokół obszarów czarnych
        for (const auto& contour_black : contours_black) {
            Rect rect = boundingRect(contour_black);
            rectangle(image, rect, Scalar(0, 255, 0), 2);
        }

        // Przejdź przez kontury i narysuj prostokąty wokół obszarów żółtych
        for (const auto& contour_yellow : contours_yellow) {
            Rect rect = boundingRect(contour_yellow);
            rectangle(image, rect, Scalar(0, 255, 0), 2);
        }

        // Wyświetl obraz z naniesionymi prostokątami
        imshow("Detekcja Pszczoł", image);
        waitKey(0);
        destroyAllWindows();

        // Przywrócenie standardowego strumienia wyjścia
        logFile.close();
    } catch (const std::exception& e) {
        std::cerr << "Wystąpił wyjątek: " << e.what() << std::endl;
        throw; // Rzucenie wyjątku dalej, aby informować o problemie na wyższym poziomie
    }

    logFile.close(); // Zamknięcie pliku logowania
}

// Funkcja do testowania połączenia z bazą danych
TEST(DatabaseTest, ConnectToDatabase) {
    sqlite3* db = nullptr;
    try {
        db = connect_to_database();
    } catch (const std::exception& e) {
        FAIL() << "Nie udało się połączyć z bazą danych: " << e.what();
    }
    sqlite3_close(db);
}

// Test sprawdzający, czy tabela została utworzona poprawnie
TEST(DatabaseTest, CreateTable) {
    sqlite3* db = nullptr;
    try {
        db = connect_to_database();
        create_tables(db);

        // Sprawdź, czy tabela została utworzona poprawnie
        bool tableExists = false;
        const char* query = "SELECT name FROM sqlite_master WHERE type='table' AND name='ul_pszczole';";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* tableName = sqlite3_column_text(stmt, 0);
                if (tableName) {
                    std::string tableNameStr(reinterpret_cast<const char*>(tableName));
                    if (tableNameStr == "ul_pszczole") {
                        tableExists = true;
                    }
                }
            }
            sqlite3_finalize(stmt);
        }

        ASSERT_TRUE(tableExists);
    } catch (const std::exception& e) {
        FAIL() << "Błąd podczas tworzenia tabeli: " << e.what();
    }

    sqlite3_close(db);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}