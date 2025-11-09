// advanced_library.cpp
// A compact but feature-rich demo: Library management with OOP, templates, threads, file I/O.
// Compile: g++ -std=c++17 -pthread advanced_library.cpp -o advanced_library

#include <bits/stdc++.h>
using namespace std;
using chrono_ms = chrono::milliseconds;

// ---------- Utilities ----------
static inline string trim(const string &s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static inline vector<string> split(const string &s, char delim = '|') {
    vector<string> out;
    string cur;
    stringstream ss(s);
    while (getline(ss, cur, delim)) out.push_back(cur);
    return out;
}

// ---------- Book class (domain model) ----------
class Book {
public:
    using id_t = unsigned long long;

    Book() = default;
    Book(id_t id, string title, string author, int year)
        : id_(id), title_(move(title)), author_(move(author)), year_(year), checked_out_(false) {}

    virtual ~Book() = default;

    id_t id() const noexcept { return id_; }
    const string& title() const noexcept { return title_; }
    const string& author() const noexcept { return author_; }
    int year() const noexcept { return year_; }
    bool checked_out() const noexcept { return checked_out_; }

    void check_out() {
        if (checked_out_) throw runtime_error("Book already checked out");
        checked_out_ = true;
    }
    void return_back() {
        if (!checked_out_) throw runtime_error("Book is not checked out");
        checked_out_ = false;
    }

    // Simple text serialization: id|title|author|year|checked
    virtual string serialize() const {
        // replace any '|' in fields with '/'
        auto safe = [](const string &s) {
            string r = s;
            replace(r.begin(), r.end(), '|', '/');
            return r;
        };
        return to_string(id_) + "|" + safe(title_) + "|" + safe(author_) + "|" + to_string(year_) + "|" + (checked_out_ ? "1" : "0");
    }

    static unique_ptr<Book> deserialize(const string &line) {
        auto parts = split(line, '|');
        if (parts.size() < 5) throw runtime_error("Bad record: " + line);
        id_t id = stoull(parts[0]);
        string title = parts[1];
        string author = parts[2];
        int year = stoi(parts[3]);
        bool checked = (parts[4] == "1");
        auto b = make_unique<Book>(id, title, author, year);
        if (checked) b->checked_out_ = true;
        return b;
    }

    virtual void print(ostream &os = cout) const {
        os << "[" << id_ << "] \"" << title_ << "\" by " << author_ << " (" << year_ << ")"
           << (checked_out_ ? " [checked out]" : " [available]") << "\n";
    }

protected:
    id_t id_ = 0;
    string title_;
    string author_;
    int year_ = 0;
    bool checked_out_ = false;
};

// ---------- Generic repository ----------
template<typename T>
class Repository {
public:
    using ptr = unique_ptr<T>;

    Repository() = default;

    // Add returns pointer to the added object (raw pointer owned by repo)
    T* add(ptr obj) {
        lock_guard<mutex> g(lock_);
        T* raw = obj.get();
        items_.push_back(move(obj));
        return raw;
    }

    // Remove by predicate; returns number removed
    template<typename Pred>
    size_t remove_if(Pred p) {
        lock_guard<mutex> g(lock_);
        auto old = items_.size();
        items_.erase(remove_if(items_.begin(), items_.end(),
                               [&](const ptr &u){ return p(*u); }), items_.end());
        return old - items_.size();
    }

    template<typename Pred>
    vector<T*> find_all(Pred p) {
        lock_guard<mutex> g(lock_);
        vector<T*> out;
        for (auto &u : items_) if (p(*u)) out.push_back(u.get());
        return out;
    }

    T* find_by_id(typename T::id_t id) {
        lock_guard<mutex> g(lock_);
        for (auto &u : items_) if (u->id() == id) return u.get();
        return nullptr;
    }

    vector<T*> all() {
        lock_guard<mutex> g(lock_);
        vector<T*> out; out.reserve(items_.size());
        for (auto &u : items_) out.push_back(u.get());
        return out;
    }

    // Save and load using the T::serialize / T::deserialize contract
    void save_to_file(const string &filename) {
        lock_guard<mutex> g(lock_);
        ofstream fout(filename, ios::trunc);
        if (!fout) throw runtime_error("Failed to open " + filename + " for writing");
        for (auto &u : items_) fout << u->serialize() << "\n";
    }

    void load_from_file(const string &filename) {
        lock_guard<mutex> g(lock_);
        ifstream fin(filename);
        if (!fin) return; // no file yet is okay
        items_.clear();
        string line;
        while (getline(fin, line)) {
            line = trim(line);
            if (line.empty()) continue;
            try {
                auto p = T::deserialize(line);
                items_.push_back(move(p));
            } catch (exception &e) {
                cerr << "Warning: skipping bad record: " << e.what() << "\n";
            }
        }
    }

private:
    vector<ptr> items_;
    mutable mutex lock_;
};

// ---------- Library Controller ----------
class Library {
public:
    Library(const string &dbfile = "library_db.txt") : dbfile_(dbfile), next_id_(1), stop_autosave_(false) {
        repo_.load_from_file(dbfile_);
        // compute next id
        auto all = repo_.all();
        for (auto p : all) if (p->id() >= next_id_) next_id_ = p->id() + 1;

        // start autosave thread
        autosave_thread_ = thread([this]() {
            while (!stop_autosave_.load()) {
                this_thread::sleep_for(chrono::seconds(10));
                try {
                    save();
                } catch (exception &e) {
                    cerr << "Autosave error: " << e.what() << "\n";
                }
            }
        });
    }

    ~Library() {
        stop_autosave_.store(true);
        if (autosave_thread_.joinable()) autosa
