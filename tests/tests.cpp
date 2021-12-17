#include <cpprealm/sdk.hpp>

struct Dog: realm::object {
    realm::persisted<std::string> name;
    realm::persisted<int> age;

    using schema = realm::schema<"Dog",
                                 realm::property<"name", &Dog::name>,
                                 realm::property<"age", &Dog::age>>;
};

struct Person: realm::object {
    realm::persisted<std::string> name;
    realm::persisted<int> age;
    realm::persisted<std::optional<Dog>> dog;

    using schema = realm::schema<"Person",
                                 realm::property<"name", &Person::name>,
                                 realm::property<"age", &Person::age>,
                                 realm::property<"dog", &Person::dog>>;
};

struct AllTypesObjectLink: realm::object {
    realm::persisted<int> _id;
    realm::persisted<std::string> str_col;

    using schema = realm::schema<"AllTypesObjectLink", realm::property<"_id", &AllTypesObjectLink::_id, true>, realm::property<"str_col", &AllTypesObjectLink::str_col>>;
};

struct AllTypesObject: realm::object {
    enum class Enum {
        one, two
    };

    realm::persisted<int> _id;
    realm::persisted<Enum> enum_col;
    realm::persisted<std::chrono::time_point<std::chrono::system_clock>> date_col;
    realm::persisted<realm::uuid> uuid_col;
    realm::persisted<std::vector<std::uint8_t>> binary_col;

    realm::persisted<std::vector<int>> list_int_col;
    realm::persisted<std::vector<AllTypesObjectLink>> list_obj_col;

    using schema = realm::schema<
        "AllTypesObject",
        realm::property<"_id", &AllTypesObject::_id, true>,
        realm::property<"date_col", &AllTypesObject::date_col>,
        realm::property<"uuid_col", &AllTypesObject::uuid_col>,
        realm::property<"binary_col", &AllTypesObject::binary_col>,
        realm::property<"list_int_col", &AllTypesObject::list_int_col>,
        realm::property<"list_obj_col", &AllTypesObject::list_obj_col>>;
};

static auto success_count = 0;
static auto fail_count = 0;
template <typename T, typename V>
bool assert_equals(const T& a, const V& b)
{
    if (a == b) { success_count += 1; }
    else { fail_count += 1; }
    return a == b;
}

#define assert_equals(a, b) \
if (!assert_equals(a, b)) {\
std::cerr<<__FILE__<<" L"<<__LINE__<<": "<<#a<<" did not equal "<<#b<<std::endl;\
}

namespace test {
struct task_base {
    struct promise_type {
        coroutine_handle<> precursor;

        task_base get_return_object() noexcept {
            return task_base{coroutine_handle<promise_type>::from_promise(*this)};
        }

        suspend_never initial_suspend() const noexcept { return {}; }

        void unhandled_exception() {
            if (auto err = std::current_exception())
                std::rethrow_exception(err);
        }

        auto final_suspend() const noexcept {
            struct awaiter {
                bool await_ready() const noexcept {
                    return false;
                }

                void await_resume() const noexcept {
                }
                coroutine_handle<> await_suspend(coroutine_handle<promise_type> h) noexcept {
                    auto precursor = h.promise().precursor;
                    if (precursor) {
                        return precursor;
                    }
                    return noop_coroutine();
                }
            };
            return awaiter{};
        }

        void return_void() noexcept {
        }
    };
    coroutine_handle<promise_type> handle;
    std::string path;
};
template <realm::StringLiteral TestName>
struct task : task_base {
    bool await_ready() const noexcept {
        return handle.done();
    }

    void await_resume() const noexcept {
        handle.promise();
    }

    void await_suspend(coroutine_handle<> coroutine) const noexcept {
        handle.promise().precursor = coroutine;
    }

    task() : task_base(TestName) {}
    task(task_base&& t) : task_base({t.handle, TestName}) {}
};
}
#define TEST(fn) \
test::task<#fn> fn(std::string path = std::string(std::filesystem::current_path() / std::string(#fn)) + ".realm")

TEST(all) {
    auto realm = realm::open<Person, Dog>({.path=path});

    auto person = Person();
    person.name = "John";
    person.age = 17;
    person.dog = Dog{.name = "Fido"};

    realm.write([&realm, &person] {
        realm.add(person);
    });

    assert_equals(*person.name, "John");
    assert_equals(*person.age, 17);
    auto dog = **person.dog;
    assert_equals(*dog.name, "Fido");

    auto token = person.observe<Person>([](auto&& change) {
        assert_equals(change.property.name, "age");
        assert_equals(std::any_cast<int>(*change.property.new_value), 19);
    });

    realm.write([&person] {
        person.age += 2;
    });

    assert_equals(*person.age, 19);

    auto persons = realm.objects<Person>();
    assert_equals(persons.size(), 1);

    std::vector<Person> people;
    std::copy(persons.begin(), persons.end(), std::back_inserter(people));
    for (auto& person:people) {
        realm.write([&person, &realm]{
            realm.remove(person);
        });
    }

    assert_equals(persons.size(), 0);
    auto app = realm::App("car-wsney");
    auto user = co_await app.login(realm::App::Credentials::anonymous());

    auto tsr = co_await user.realm<AllTypesObject, AllTypesObjectLink>("foo");
    auto synced_realm = tsr.resolve();
    synced_realm.write([&synced_realm]() {
        synced_realm.add(AllTypesObject{._id=1});
    });

    assert_equals(*synced_realm.object<AllTypesObject>(1)._id, 1);

    co_return;
}

// MARK: Test List
TEST(list) {
    auto realm = realm::open<AllTypesObject, AllTypesObjectLink, Dog>({.path=path});
    auto obj = AllTypesObject{};
    obj.list_int_col.push_back(42);
    assert_equals(obj.list_int_col[0], 42);

    obj.list_obj_col.push_back(AllTypesObjectLink{.str_col="Fido"});
    assert_equals(obj.list_obj_col[0].str_col, "Fido");
    assert_equals(obj.list_int_col.size(), 1);
    for (auto& i : obj.list_int_col) {
        assert_equals(i, 42);
    }
    realm.write([&realm, &obj]() {
        realm.add(obj);
    });

    assert_equals(obj.list_int_col[0], 42);
    assert_equals(obj.list_obj_col[0].str_col, "Fido");

    realm.write([&obj]() {
        obj.list_int_col.push_back(84);
        obj.list_obj_col.push_back(AllTypesObjectLink{._id=1, .str_col="Rex"});
    });
    size_t idx = 0;
    for (auto& i : obj.list_int_col) {
        assert_equals(i, obj.list_int_col[idx]);
        ++idx;
    }
    assert_equals(obj.list_int_col.size(), 2);
    assert_equals(obj.list_int_col[0], 42);
    assert_equals(obj.list_int_col[1], 84);
    assert_equals(obj.list_obj_col[0].str_col, "Fido");
    assert_equals(obj.list_obj_col[1].str_col, "Rex");
    co_return;
}

TEST(thread_safe_reference) {
    auto realm = realm::open<Person, Dog>({.path=path});

    auto person = Person { .name = "John", .age = 17 };
    person.dog = Dog {.name = "Fido"};

    realm.write([&realm, &person] {
        realm.add(person);
    });

    auto tsr = realm::thread_safe_reference<Person>(person);
    std::condition_variable cv;
    std::mutex cv_m;
    bool done;
    auto t = std::thread([&cv, &tsr, &done, &path]() {
        auto realm = realm::open<Person, Dog>({.path=path});
        auto person = realm.resolve(std::move(tsr));
        assert_equals(*person.age, 17);
        realm.write([&] { realm.remove(person); });
    });
    t.join();
    co_return;
}

TEST(query) {
    auto realm = realm::open<Person, Dog>({.path=path});

    auto person = Person { .name = "John", .age = 42 };
    realm.write([&realm, &person](){
        realm.add(person);
    });

    auto results = realm.objects<Person>().where("age > $0", {42});
    assert_equals(results.size(), 0);
    results = realm.objects<Person>().where("age = $0", {42});
    assert_equals(results.size(), 1);
    co_return;
}

TEST(binary) {
    auto realm = realm::open<AllTypesObject, AllTypesObjectLink>({.path=path});
    auto obj = AllTypesObject();
    obj.binary_col.push_back(1);
    obj.binary_col.push_back(2);
    obj.binary_col.push_back(3);
    realm.write([&realm, &obj] {
        realm.add(obj);
    });
    realm.write([&realm, &obj] {
        obj.binary_col.push_back(4);
    });
    assert_equals(obj.binary_col[0], 1);
    assert_equals(obj.binary_col[1], 2);
    assert_equals(obj.binary_col[2], 3);
    assert_equals(obj.binary_col[3], 4);
    co_return;
}

TEST(date) {
    auto realm = realm::open<AllTypesObject, AllTypesObjectLink>({.path=path});
    auto obj = AllTypesObject();
    assert_equals(obj.date_col, std::chrono::time_point<std::chrono::system_clock>{});
    auto now = std::chrono::system_clock::now();
    obj.date_col = now;
    assert_equals(obj.date_col, now);
    realm.write([&realm, &obj] {
        realm.add(obj);
    });
    assert_equals(obj.date_col, now);
    realm.write([&realm, &obj] {
        obj.date_col += std::chrono::seconds(42);
    });
    assert_equals(obj.date_col, now + std::chrono::seconds(42));
    co_return;
}

struct Foo: realm::object {
    realm::persisted<int> bar;
    Foo() = default;
    Foo(const Foo&) = delete;
    using schema = realm::schema<"Foo", realm::property<"bar", &Foo::bar>>;
};

int main() {

    std::vector<test::task_base> tasks = {
        all(),
        thread_safe_reference(),
        list(),
        query(),
        binary(),
        date()
    };
    {
        while (std::transform_reduce(tasks.begin(), tasks.end(), true,
                                     [](bool done1, bool done2) -> bool { return done1 && done2; },
                                     [](const auto& task) -> bool { return task.handle.done(); }) == false) {
        };
    }
    for (auto& task : tasks) {
        auto path = task.path;
        std::filesystem::remove(std::filesystem::current_path() / std::string(path + ".realm"));
        std::filesystem::remove(std::filesystem::current_path() / std::string(path + ".realm.lock"));
        std::filesystem::remove(std::filesystem::current_path() / std::string(path + ".realm.note"));
    }

    std::cout<<success_count<<"/"<<success_count + fail_count<<" checks completed successfully."<<std::endl;
    return fail_count;
}

//@end
