#include <regex>
#include <random>
#include <algorithm>
#include <iostream>
#include <memory>
#include <cctype>
#include <deque>
#include <map>
#include <set>

#include "printf.hh"

static std::mt19937 rnd;

// frand() generates a random number between 0 and 1.
#define frand()    std::uniform_real_distribution<>(0.f, 1.f)(rnd)
// Generate a random number of specified range.
#define rand(size) std::uniform_int_distribution<>(0, (size)-1)(rnd)
// Return the size of an array at compile time.
template<typename T, std::size_t size>
constexpr std::size_t count(const T(&) [size])
{
    return size;
}

// Syntactic shorthand for creating regular expressions.
static std::regex operator ""_r(const char* pattern, std::size_t length)
{
    return std::regex(pattern,length);
}



// English language word manipulations:
static std::string RemoveArticle(const std::string& s)
{
    return std::regex_replace(s, "^(?:a|an|the) +"_r, "");
}

static std::string Pluralize(const std::string& s)
{
    // Make the name plural by tacking a 's' at the right spot
    // which is usually in the end of the string, but always
    // before any "of" or "made of".

    // Put a placeholder "\001" there first, so we can use it
    // later to decide the correct suffix depending on the word.
    auto temp = std::regex_replace(
        s,
        R"(^(.*?)( (?:\(|of\b|made of\b).*)?$)"_r,
        "$1" "\001" "$2");

    // The correct form of plural suffix depends on how the word ends.
    // This table handles most cases occurring in the game correctly.
    // It is by no means a complete reference for English inflection.
    #define endswith(s)  std::regex_search(temp,  "(?:" s ")\001"_r)
    #define replace(s,w) std::regex_replace(temp, "(?:" s ")\001"_r, w)
    return
        endswith("s")       ? temp                  // leggings, overalls
      : endswith("y")       ? replace("y",   "ies") // berry
      : endswith("o|sh|ss") ? replace("",    "es")  // dish, glass, potato
      : endswith("ff?")     ? replace("ff?", "ves") // staff, wolf
      :                       replace("",    "s");  // anything else
    #undef endswith
    #undef replace
}

static std::string AddArticle(const std::string& s, bool definite = false)
{
    std::string p = RemoveArticle(s);
    if(definite) return "the " + p;
    // Don't add "a" or "an" to a plural form
    if(p == Pluralize(p)) return p;
    // Add "an" if the word begins with a vowel, "a" otherwise.
    return (std::regex_search(p, "^[aeiou]"_r) ? "an ":"a ") + p;
}

static std::string UCfirst(const std::string& s)
{
    return char(std::toupper(s[0])) + s.substr(1);
}

static const char* const Numerals1to12[12-1+1] =
    { "one","two","three","four","five","six","seven",
      "eight","nine","ten","eleven","twelve" };

static std::string ListWithCounts(std::deque<std::string>&& items, bool oneliner=true)
{
    // Count the number of times each item occurs
    std::map<std::string, unsigned> count;
    for(const auto& s: items) ++count[s];
    // Now, deal with each item
    for(size_t a=0; a<items.size(); ++a)
    {
        std::string& n = items[a];
        auto i = count.find(n);
        // Was this item one of those duplicated ones?
        if(i->second == 1) continue;
        // Have we already dealt with it?
        if(!i->second)
        {
            // Yes, delete it
            items.erase( items.begin() + a-- );
            continue;
        }
        // Remove possible indefinite article.
        n = RemoveArticle(n);
        // Add the count. Numbers 2-12 are expressed using an English word.
        if(i->second <= 12) n = "%s %s"_f % Numerals1to12[i->second-1] % n;
        else                n = "%u %s"_f % i->second % n;
        n = Pluralize(n);
        // Remember to not do the same item again
        i->second = 0;
    }
    // Finally convert the list into text
    std::string output;
    for(std::size_t a=0; a<items.size(); ++a)
        if(oneliner)
        {
            if(a) output += (a+1==items.size()) ? ", and " : ", ";
            output += items[a];
        }
        else
            output += "%s\n"_f % items[a];
    return output;
}

enum { Normal=64, Bold=128, ColorMask=63 };
static const std::map<std::string, unsigned> ansi_features =
{ {"dfl",     0},
  {"reset",  37|Normal},
  {"chest",  35|Normal},
  {"cart",   34|Bold},
  {"me",     36|Bold},
  {"exit",   33|Bold},
  {"wall",   30|Bold},
  {"road",   33|Normal},
  {"items" , 32|Normal},
  {"alert",  31|Bold},
  {"prompt", 37|Bold},
  {"flush",  1 } };
/* Support for color terminals */
struct Term
{
    int color=37;
    bool bold=false, enabled=true;

    std::string format(const std::string& what)
    {
        static std::regex pat = "`([a-z]+)`|([^`]+|.)"_r;
        std::string result;
        std::smatch res;
        for(auto b = what.begin(), e = what.end(); std::regex_search(b,e, res, pat); b = res[0].second)
            if(res[2].length())
                result += res[2];
            else
            {
                auto i = ansi_features.find(res[1]);
                if(i != ansi_features.end())
                    switch(int c = i->second)
                    {
                        case 0: color = 0; break;
                        case 1: std::cout << std::flush; break;
                        default: result += SetColor( c&Bold, c&ColorMask );
                    }
            }
        return result;
    }

    Term& operator<< (const std::string& what)
    {
        std::cout << format(what);
        return *this;
    }

    std::string SetColor(bool newbold,int newcolor)
    {
        if(((newbold != bold) || newcolor != color) && enabled)
            return "\33[%d;%dm"_f % (bold=newbold) % (color=newcolor);
        return {};
    }
    void EnableDisable(bool state)
    {
        enabled = state;
        if(enabled) *this << "`dfl`";
    }
} static term;

struct ItemReference
{
    // Was this "all" without any specifiers?
    // If so, then refs should contain just 1 element with what=""
    bool everything = false;

    // Original request
    std::string original;

    struct SingleReference
    {
        // What was referred to? blank = everything
        std::string what;
        // Amount, if given. 0 = 1 shirt, or 1 pile of coins (default)
        //                   1 = 1 shirt, or 1 coin
        //                   2 = 2 shirts, or 2 coins
        long        amount = 0;
        // Index, if given.  0 = every item matching the description
        //                   1 = first item matching the description (default)
        //                   2 = second item matching the description
        // Ignored for money
        long        index = 1;
    };
    std::deque<SingleReference> refs, except;

    ItemReference(const char* what) : ItemReference( std::string(what) ) {}

    ItemReference(const std::string& what)
    {
        std::smatch res;
        std::regex_match(what, res, "(.*?)(?: except (.+))?"_r);
        original = res[1];

        // For "all"-type entries, add a dummy entry that indicates "everything"
        if(original == "all" || original == "everything")
        {
            everything = true;
            SingleReference w;
            w.index = 0;
            refs.push_back(w);
        }
        else if(!original.empty())
        {
            // Deal with a comma-separated list of operands
            ParseReferences(refs, original);
        }

        if(res[2].length()) ParseReferences(except, res[2]);
    }

    // True if this request clearly intends to address only one specific item
    bool IsSpecific() const
    {
        return !everything && refs.size() == 1 && refs[0].amount <= 1 && refs[0].index >= 1;
    }

    void ParseReferences(std::deque<SingleReference>& list, const std::string& what)
    {
        std::regex pat(" *((?:(?! *,| and | *$).)+)(?:[ ,]|and )*");
        std::smatch res;
        for(auto b = what.begin(); std::regex_search(b, what.end(), res, pat); b = res[0].second)
            list.push_back( ParseSingleReference( res[1] ) );
    }
    SingleReference ParseSingleReference(const std::string& part) const
    {
        SingleReference w;
        // Read the item count from the begin of the string.
        std::string word = part;
        for(unsigned a=1; a<=12; ++a)
            word = std::regex_replace(word,
                std::regex( (R"(^%s\b)"_f % Numerals1to12[a-1]).str() ),
                ("%u"_f % a).str());
        static std::regex pattern("^((all|[0-9]+) +)? *(.*)");
        std::smatch res;
        std::regex_match(word, res, pattern);
        w.what                 = res[3]; // Item name without number
        std::string number_str = res[2]; // The number, if any; empty otherwise
        if(number_str == "all")
            w.index = 0;
        else if(!number_str.empty())
            w.amount = std::stol(number_str);
        else
        {
            // Read the possible item index from the end of the string.
            static std::regex pattern("^(.*?)(?: +([0-9]+))?$");
            std::smatch res;
            std::regex_match(w.what, res, pattern);
            w.what             = res[1]; // Item name without number
            number_str         = res[2]; // The number, if any; empty otherwise
            if(!number_str.empty())
                w.index = std::stol(number_str);
        }
        return w;
    }
};

/* First, define some stuff for our adventure. */
struct GenericData
{
    // These three generic fields will provide most of our data needs.
    const char* name;
    float worth;
    float weight;
} static const
// Define types of coins. Reference value: 1.0 = gold. Each coin weighs 0.01 units.
MoneyTypes[] =
    { {"platinum",10, 0.01},    {"gold",     1, 0.01},    {"silver", 0.6,  0.01},
      {"bronze", 0.4, 0.01},    {"copper", 0.2, 0.01},    {"wood",   0.01, 0.01} },
// Define conditions for items. Reference value: 1.0 = excellent.
// Three most common condition types are listed first.
CondTypes[] =
    { {"awesome",  1.2, 0},     {"excellent", 1,   0},    {"good",   0.9, 0},
      {"average",  0.75,0},     {"poor",      0.5, 0},    {"bad",    0.6, 0},
      {"thrashed", 0.4, 0} },
// Define building materials. The raw material cost is included, and the weight.
// Two most common build types are listed first.
BuildTypes[] =
    { {"iron",    0.4, 3  },    {"fur",    0.01,0.2},     {"gold",    1,   3.5},
      {"bronze",  0.1, 2.7},    {"pewter", 0.05,2  },     {"chromium",0.9, 2  },
      {"platinum",2,   4  },    {"bamboo", 0.01,1  },     {"leather", 0.09,0.5},
      {"silk",    0.03,0.1},    {"steel",  0.7, 3  },     {"glass",   0.04,2  } },
// List of all possible treasures. Each treasure has a "type", a "build material"
// and a "condition", which together define the item's worth in money.
// Additionally, the player may occasionally find raw money.
// The worth multiplier and weight multiplier are provided.
// Reference values: shirt = 1.0 worth, 1.0 weight.
// Four most common item types are listed first.
// Most of these are armour items, because our material list
// includes both hard materials (metals etc) and soft materials (silk etc).
ItemTypes[] =
    { {"shirt",     1,   1   }, {"shoe",     0.4, 1   },  {"bracelet", 0.2, 0.2 },
      {"tie",       0.25,0.25}, {"sceptre",  4,   2.5 },  {"crown",    3,   0.6 },
      {"leggings",  0.8, 0.5 }, {"dagger",   0.1, 1.5 },  {"cap",      0.6, 0.5 },
      {"battlesuit",10,  5.0 }, {"hammer",   0.4, 3.0 },  {"cape",     0.7, 1   },
      {"overalls",  4,   4.0 } },
// List of bodyparts that the player may get sprained, plus cost in hitpoints.
BodyParts[] =
    { {"finger", 10,0},         {"elbow",    60,0},       {"teeth", 30,0},
      {"toe",    40,0},         {"shoulder", 100,0} },
// List of different kinds of tunnels. It is just for variance.
EnvTypes[] =
    { {"dark",     0,0},        {"tall",   0,0},          {"humid", 0,0},
      {"beautiful",0,0},        {"narrow", 0,0} },
// Finally, a list of achievements.
// They are crypted with a reversible cipher to prevent spoiling
// the game to a person who happens to glance over the source code.
FoodTypes[] =
    { {"b akbdl epqfts dblf",50000,0}, {"b kbqhf okbsf pe dgjdlfm kfht",35000,0},
      {"b dbvkcqpm pe dpplfc opsbspft",20000,0}, {"b dgjdlfm gps cph",10000,0},
      {"dgfftf bmc nbdbqpmj",6000,0}, {"b avssfqnjkl ajtdvjs",3000,0}, {"b apjkfc fhh",2000,0},
      {"tpnf kjdgfm tsfx",1000,0}, {"b xppc dpqsfw tbmcxjdg",700,0},
      {"b dvo pe ujmfhbq", 500,0}, {"b dvo pe bookf tffct", 300,0},
      {"b qpssfm dbqqps", 200,0}, {"b nvh pe nvccz xbsfq",110,0}, {"tpnf qbaajs cqpoojmht",70,0},
      {"b cfbc dpdlqpbdg", 50,0}, {"b npmsg pkc tojcfq xfa", 30,0},
      {"b gjkk pe cvts", 16,0}, {"b gfbo pe cvts", 8,0}, {"b ajh ojkf pe cvts", 4,0},
      {"b ojkf pe cvts", 2,0}, {"b tofdlkf pe cvts", 1,0}
    }; // key: badcfehgjilknmporqtsvuxwzy

// Determine how well the player character
// could eat by selling all their treasures.
static std::string Appraise(double value, int v=1, std::size_t maxi=3)
{
    std::deque<std::string> list; redo:
    for(const auto& f: FoodTypes)
        if(value >= f.worth)
        {
            std::string k = f.name;
            for(auto& c: k) if(c-' ') c=1+((c-1)^v);
            list.push_back(k); value -= f.worth;
            if(list.size() < maxi) goto redo;
            break;
        }
    if(list.empty()) return "nothing at all";
    return ListWithCounts( std::move(list) );
}

struct ItemType
{
    // Any item has these three attributes.
    std::size_t type      = frand() > 0.4 ? rand(count(ItemTypes))  : rand(4);
    std::size_t build     = frand() > 0.4 ? rand(count(BuildTypes)) : rand(2);
    std::size_t condition = frand() > 0.8 ? rand(count(CondTypes))  : rand(3);

    // If this is a chest, the above three values are ignored and this is nonzero.
    float       chest = 0.f;
    // If this is a cart, this is non-null and the others are irrelevant.
    std::shared_ptr<struct Eq> cart;

    std::string GetType() const;
    std::string GetMaterial() const;
    std::string GetCondition() const;

    // There are various ways that an item can be called.
    //       mat=1:    changes "shirt" into "silk shirt"
    //       mat=2:    changes "shirt" into "shirt made of silk"
    //       cond=1:   changes "shirt" into "awesome shirt"
    std::string name(int cond=0, int mat=0) const;
    std::string look(bool specific) const;

    // Calculate the weight and monetary value of an item.
    float weight() const
    {
        if(chest > 0.f || cart) return 999.f;
        return BuildTypes[build].weight
             * ItemTypes[type].weight;
    }
    float value(float constant=300.f) const
    {
        if(chest > 0.f || cart) return 0.f;
        return constant * BuildTypes[build].worth
                        * ItemTypes[type].worth
                        * CondTypes[condition].worth;
    }
    bool immovable() const
    {
        return chest > 0.f || cart;
    }
};

// Collection of items and money, either in character's pocket,
// on the ground, or in a container.
struct Eq
{
    std::deque<ItemType> Items;
    long Money[ count(MoneyTypes) ] = { 0 };

    // Calculate the total worth of all these items and coins.
    float value() const
    {
        // Count all the money
        float result = 0.f; size_t a=0;
        for(auto m: Money)        result += m * MoneyTypes[a++].worth;
        // Add the worth of all items.
        for(const auto& i: Items) result += i.value();
        return result;
    }
    // Calculate the total weight of all these items and coins.
    float weight() const
    {
        // Count all the money
        float result = 0.f; size_t a=0;
        for(auto m: Money)        result += m * MoneyTypes[a++].weight;
        // Add the weight of all items.
        for(const auto& i: Items) result += i.weight();
        return result;
    }
    long burden() const
    {
        return 1 + weight();
    }
    std::size_t count_items() const
    {
        std::size_t result = Items.size();
        for(auto l: Money) if(l) ++result;
        return result;
    }

    // Clear the list of items (or generate N random items).
    void clear(std::size_t n = 0)
    {
        Items.resize(n);
        for(auto& m: Money) m = 0;
    }

    // Generate the output for "looking at" an item.
    //    n   = Which item to look at
    //    in  = true, if the player wants to look "in" the item
    //    all = true, if this is not the only item being listed at once
    std::pair<std::string, float> look_item(std::size_t n, bool specific) const
    {
        const auto& item = Items[n];
        return { item.look(specific), item.value() };
    }

    // Generate the output for "looking at" money.
    std::pair<std::string, float> look_money(long m, bool specific) const
    {
        std::string info, common =
            "%ld %s %s\n"_f % Money[m] % MoneyTypes[m].name
                            % (Money[m]==1 ? "coin" : "coins");
        if(specific)
            info = "The coins are worth %.2f gold total.\n"_f % (Money[m] * MoneyTypes[m].worth);
        return { common + info, Money[m] * MoneyTypes[m].worth };
    }

    // Generate the output for checking out the whole inventory.
    //   is_inv        = false if this is not player's inventory.
    //   retval.second = false if the inventory is empty.
    std::pair<std::string, bool> print(bool is_inv) const
    {
        float itemsvalue = 0.f, moneyvalue = 0.f;
        std::string result;

        // List all items and count their total value.
        std::deque<std::string> names;
        for(const auto& i: Items)
        {
            names.push_back( AddArticle(i.name(0, 1)) );
            itemsvalue += i.value();
        }

        result += ListWithCounts( std::move(names), false);

        if(is_inv && itemsvalue != 0.f)
            result += "The total value of your items is %.2f gold.\n"_f % itemsvalue;

        // List all coins and count their total value.
        size_t a=0;
        for(auto m: Money)
        {
            auto v = look_money(a++, false);
            if(m) result += v.first;
            moneyvalue += v.second;
        }

        if(is_inv && moneyvalue != 0.f)
            result += "The coins are worth %.2f gold total.\n"_f % moneyvalue;

        // Also report the total weight of everything.
        if(is_inv)
            result +=
                "Your possessions wear you down %ld points for every step you take.\n"
                "You estimate that these possessions could earn you %s.\n"_f
                % burden() % Appraise(value());

        return { result, moneyvalue != 0.f || !Items.empty() };
    }

    // Finds money matching the given keywords. -1 = no money found
    // We accept the following types of expressions:
    //     gold        -- matches gold coins
    //     gold coin   -- matches gold coins
    //     gold coins  -- matches gold coins
    //     coins       -- matches all coins
    //     blank/all   -- matches all coins
    // Ignores "amount" and "index" in the SingleReference.
    long find_money(const ItemReference::SingleReference& w, std::size_t first=0) const
    {
        // For each type of coins that does exist, accept it,
        // if it matches the user's request.
        for(std::size_t m = first; m < count(MoneyTypes); ++m)
            if(Money[m] > 0
            && std::regex_match(w.what,
                std::regex( ("|money|coins?|%s( coins?)?"_f % MoneyTypes[m].name).str() )
                               )) return m;
        return -1;
    }
    // Finds items matching the given keywords. -1 = no item found.
    // We accept the following types of expressions:
    //     an awesome gold sceptre  -- matches awesome gold sceptre
    //     awesome gold sceptre     -- matches awesome gold sceptre
    //     a gold sceptre           -- matches any gold sceptre
    //     gold sceptre             -- matches any gold sceptre
    //     an awesome sceptre       -- matches any awesome sceptre
    //     awesome sceptre          -- matches any awesome sceptre
    //     a sceptre                -- matches any sceptre
    //     sceptre                  -- matches any sceptre
    //     sceptres                 -- matches any sceptre
    //     shirt 2                  -- only matches the second shirt
    //     blank/all                -- matches anything
    // Ignores "amount" in the SingleReference
    long find_item(const ItemReference::SingleReference& w, std::size_t first=0) const
    {
        // From more specific to less specific,
        // check if we found what the player asked for.
        long occurrences = 0;
        for(std::size_t a = 0; a < Items.size(); ++a)
            for(int level=3*2*4-1; level>=0; --level)
            {
                std::string n = Items[a].name((level/3)%2, level%3);
                if(level/6 == 1) n = AddArticle(n, false);
                if(level/6 == 2) n = AddArticle(n, true);
                if(level/6 == 3) n = Pluralize(n);
                if(w.what.empty() || w.what == n)
                {
                    // break = continue item loop
                    if(w.index && !w.amount && ++occurrences != w.index) break;
                    if(a < first) break;
                    return a;
                }
            }
        // Give up if nothing matched
        return -1;
    }

    // Move items.
    //      target    = Where to move them to.
    //      what      = What to move
    // Result: List of everything moved & not moved per request.
    // For list of accepted item names, check find_money() and find_item().
    // If any of the individual moves fails, no move is performed.
    struct moveresult
    {
        std::deque<std::string> moved;
        std::deque<std::string> notfound;
        std::deque<std::string> immovable;
    };
    moveresult move(Eq& target, const ItemReference& what)
    {
        moveresult result;
        // Make backup of both parties of the move.
        Eq target_backup = target, me_backup = *this;

        // Deal with the entire list of sub-requests
        for(const auto& w: what.refs)
        {
            bool found_item=false, found_money=false, all = !w.index;

            // Try finding items matching the description.
            // Do in two passes. First check if the request is satisfiable,
            // and then enact it.
            for(int round=1; round<=2; ++round)
            {
                long remaining_items = w.amount ? w.amount : 1;
                for(long item_id=0; (item_id = find_item(w, item_id)) >= 0; )
                {
                    if(round == 2)
                    {
                        std::string name = AddArticle(Items[item_id].name(0,1));
                        if(Items[item_id].immovable())
                        {
                            result.immovable.push_back( name );
                            ++item_id;
                        }
                        else
                        {
                            // Append the name of moved item to the move list
                            result.moved.push_back( name );
                            // Move the item from our list to the target list
                            target.Items.push_front( Items[item_id] );
                            Items.erase( Items.begin() + item_id );
                        }
                    }
                    else
                        ++item_id;

                    found_item = true;
                    if(!all && --remaining_items <= 0) break;
                }
                // Get nothing, if the user explicitly specified e.g.
                // "get 3 shirts" but there was only 2 on the ground.
                if(round == 1 && found_item && !all && remaining_items > 0)
                {
                    found_item = false;
                    break;
                }
            }

            // Then do the same for money in the same manner.
            for(int round=1; round<=2; ++round)
            {
                long remaining_money = w.amount ? w.amount : 0x7FFFFFFFl;

                // Then check out if there's money that matches this description.
                for(long money_id=0; (money_id = find_money(w, money_id)) >= 0; )
                {
                    // Calculate how many coins we can actually pick up.
                    long get_money = std::min(remaining_money, Money[money_id]);
                    if(get_money <= 0) break;

                    if(round == 2)
                    {
                        // Append the name of moved item to the move list
                        result.moved.push_back( "%ld %s %s"_f
                                                % get_money
                                                % MoneyTypes[money_id].name
                                                % (get_money==1 ? "coin" : "coins") );
                        // Move the item from our list to the target list
                        target.Money[money_id] += get_money;
                        Money[money_id] -= get_money;
                    }
                    else
                        ++money_id;

                    found_money = true;
                    remaining_money -= get_money;
                    if(!all && (!w.amount || remaining_money <= 0)) break;
                }
                // Get nothing, if the user explicitly specified
                // picking up more coins than there we found.
                if(round == 1 && found_money && w.amount && !all && remaining_money > 0)
                {
                    found_money = false;
                    break;
                }
            }

            if(!found_item && !found_money && !what.everything)
                result.notfound.push_back(w.what);
        }

        if(!what.except.empty())
        {
            // Move all the "except" stuff back
            ItemReference takeback("");
            takeback.refs = what.except;
            auto r = target.move(*this, takeback);
            // Merge the "notfound"s
            for(const auto& s: r.notfound) result.notfound.push_back(s);
            // Remove those immovables & moveds that were in "except"
            std::set<std::string> m(r.moved.begin(), r.moved.end());
            result.moved.erase(
                std::remove_if(result.moved.begin(), result.moved.end(),
                    [&m](const std::string& s) { return m.find(s) != m.end(); }),
                result.moved.end());
            std::set<std::string> i(r.immovable.begin(), r.immovable.end());
            result.immovable.erase(
                std::remove_if(result.immovable.begin(), result.immovable.end(),
                    [&i](const std::string& s) { return i.find(s) != i.end(); }),
                result.immovable.end());
        }

        if(!result.notfound.empty()) result.moved.clear();
        if(result.moved.empty())
        {
            // Restore backup.
            target = target_backup;
            *this = me_backup;
        }
        return result;
    }
} static eq;

std::string ItemType::GetType() const
{
    if(cart) return "cart";
    if(chest > 0.f) return "chest";
    return ItemTypes[type].name;
}
std::string ItemType::GetMaterial() const
{
    if(cart) return "";
    if(chest > 0.f) return "";
    return BuildTypes[build].name;
}
std::string ItemType::GetCondition() const
{
    if(cart)
    {
        std::size_t n = cart->count_items();
        if(!n)          return "empty";
        else if(n == 1) return "1 item";
        else            return "%d items"_f % n;
    }

    if(chest > 0.f && chest < 0.35f) return "battered";
    if(chest > 0.f && chest < 0.75f) return "dented";
    if(chest > 0.f)                  return "good";

    return CondTypes[condition].name;
}

std::string ItemType::name(int cond, int mat) const
{
    // For carts and chests, condition display rule is inverted.
    // It would be otherwise "always-on", but this makes
    // sure that addressing the chest in commands works.
    if(chest > 0.f) cond = (!cond && GetCondition() != "good") ? 2 : 0;
    if(cart)        cond = !cond ? 2 : 0;
    if(cart || chest > 0.f) mat = 0;

    std::string result = GetType(), m = GetMaterial();
    // Special hack: If the material is "wood", use "wooden" instead.
    if(mat  == 1) result = "%s %s"_f % (m=="wood" ? "wooden" : m) % result;
    if(cond == 1) result = "%s %s"_f % GetCondition() % result;
    if(cond == 2) result += " (%s)"_f % GetCondition();
    if(mat  == 2) result += " made of %s"_f % m;
    return result;
}

std::string ItemType::look(bool specific) const
{
    std::string info, common = specific
        ? "It is %s. It is in %s condition.\n"_f % AddArticle(name(0,2)) % GetCondition()
        : "You see %s, in %s condition.\n"_f % AddArticle(name(0,2)) % GetCondition();

    if(cart && specific)
        info += (cart->weight() == 0.f
             ? "The cart is currently empty. You can put stuff in it with 'put <items> in cart'.\n"
             : "The cart contains the following items:\n" + cart->print(false).first)
             + "Type 'pull' to pull the cart around.\n"
               "You can get items from the cart with 'get <item> from cart'.\n";

    if(chest > 0.f && specific)
        info += "It appears to be way too heavy to lift up. It is closed. You can try to 'open' it.\n";

    if(!cart && chest <= 0.f && specific)
        info = "You estimate that with it you could probably purchase %s.\n"_f
               % Appraise(value(), 1, 1);

    return common + info;
}



// Any particular room in the puzzle may contain the following:
struct Room
{
    std::size_t Wall=0, Env=0;  // Indexes
    unsigned seed = 0;          // For maze generation
    Eq items;                   // What is lying on the floor

// Create a model "default" room based on empty definitions.
} static const defaultroom;


struct Maze
{
    // A maze contains rooms.
    std::map<long/*x*/,std::map<long/*y*/,Room> > rooms;

    // Generate a room at given coordinates.
    // The "model" room will help the maze generator generate
    // similar rooms in nearby locations.
    Room& GenerateRoom(long x,long y, const Room& model, unsigned seed)
    {
        rnd.seed( y*0xc70f6907UL + x*2166136261UL );
        auto insres = rooms[x].insert( {y, model} );
        Room& room = insres.first->second;
        if(insres.second)
        {
            room.items.clear();
            float chestrand = frand();
            // If a new room was indeed inserted, make changes in it.
            room.seed  = (seed + (frand() > 0.95 ? rand(4) : 0)) & 3;
            // 10% chance for the environment type to change.
            if(frand() > 0.9) room.Env = rand(count(EnvTypes));
            if(frand() > (seed==model.seed ? 0.95 : 0.1))
                room.Wall = frand() < 0.4 ? 2 : 0;
            // Generate a few items in the room.
            room.items.clear(unsigned(std::pow(frand(), 40.0) * 8.5));
            // Sometimes make a chest too.
            if(chestrand < 0.1f) { ItemType i; i.chest = 1.f; room.items.Items.push_front(i); }
            // Sometimes make a cart.
            if(frand() < 0.005f) { ItemType i; i.cart.reset(new Eq); room.items.Items.push_front(i); }
        }
        return room;
    }
    // Describe the room with a single character.
    char Char(long x,long y) const
    {
        auto i = rooms.find(x);     if(i == rooms.end())     return ' ';
        auto j = i->second.find(y); if(j == i->second.end()) return ' ';
        if(j->second.Wall)          return '#';
        if(!j->second.items.Items.empty())
        {
            // If there is a chest or a cart, display it differently.
            for(const auto& i: j->second.items.Items) if(i.chest > 0.f) return 'c';
            for(const auto& i: j->second.items.Items) if(i.cart)        return 'r';
            return 'i';
        }
        return '.';
    }
} static maze;


// Player's location and life.
static long x=0, y=0, life=1000;
static bool pulling=false;


static bool CanMoveTo(long wherex,long wherey, const Room& model = defaultroom)
{
    if(!maze.GenerateRoom(wherex, wherey, model, 0).Wall) return true;
    return false;
}

static Room& SpawnRooms(long wherex,long wherey, const Room& model = defaultroom)
{
    Room& room = maze.GenerateRoom(wherex,wherey, model, 0);
    #define Spawn4rooms(x,y) \
        for(char p: { 1,3,5,7 }) \
            maze.GenerateRoom(x + p%3-1, y + p/3-1, room, (p+1)/2)
    Spawn4rooms(wherex,wherey);
    for(int o=1; o<5 && CanMoveTo(wherex,wherey+o, room); ++o) Spawn4rooms(wherex,wherey+o);
    for(int o=1; o<5 && CanMoveTo(wherex,wherey-o, room); ++o) Spawn4rooms(wherex,wherey-o);
    for(int o=1; o<6 && CanMoveTo(wherex-o,wherey, room); ++o) Spawn4rooms(wherex-o,wherey);
    for(int o=1; o<6 && CanMoveTo(wherex+o,wherey, room); ++o) Spawn4rooms(wherex+o,wherey);
    return room;
}

// This routine is responsible for providing the view for the player.
// It also generates new maze data.
static void Look()
{
    // Generate rooms in the field of vision of the player.
    const Room& room = SpawnRooms(x,y);

    // Generate the current map view
    std::vector<std::string> mapgraph;
    for(long yo=-4; yo<=4; ++yo)
    {
        std::string line;
        static const std::map<char,const char*> translation =
        {
            {'@',"`me`"},
            {'#',"`wall`"},
            {'c',"`chest`"},
            {'r',"`cart`"},
            {'.',"`road`"},
            {'i',"`items`"}
        };
        for(long xo=-5; xo<=5; ++xo)
        {
            char c = ((xo==0&&yo==0) ? '@' : maze.Char(x+xo, y+yo));
            auto i = translation.find(c);
            if(i != translation.end()) line += i->second;
            line += c;
        }
        mapgraph.push_back( "`dfl`%s`reset`"_f % line );
    }

    // This is the text that will be printed on the right side of the map
    const std::string info_str =
        "`reset`In a %s tunnel at %+3ld,%+3ld\n"_f % EnvTypes[room.Env].name % x % -y
      + "`reset`Exits:`exit`%s%s%s%s\n\n"_f
        % (CanMoveTo(x+0, y-1) ? " north" : "")
        % (CanMoveTo(x+0, y+1) ? " south" : "")
        % (CanMoveTo(x-1, y+0) ? " west" : "")
        % (CanMoveTo(x+1, y+0) ? " east" : "")
      + room.items.print(false).first;

    // Print the map and the information side by side.
    auto m = mapgraph.begin();
    auto b = info_str.begin(), e = info_str.end();
    auto pat = "([^\n]*)\n"_r;
    for(std::smatch res; m != mapgraph.end() || b != e; res = std::smatch{})
    {
        if(b != e) { std::regex_search(b, e, res, pat); b = res[0].second; }
        std::string sa = m!=mapgraph.end() ? *m++ : std::string(11,' ');
        std::string sb = res[1];
        term << "`dfl`%s | `items`%s\n"_f % sa % sb;
    }
}

static void EatLife(long l)
{
    const char* msg = nullptr;
    if(life>=800 && life-l<800) msg = "You are so hungry!\n";
    if(life>=150 && life-l<150) msg = "You are famished!\n";
    if(life>=70 && life-l<70) msg = "You are about to collapse any second!\n";
    life -= l;
    if(msg) { term << "`alert`%s`reset`"_f % msg; }
}

static bool TryMoveBy(int xd,int yd)
{
    // If we are moving diagonally, ensure that there is an actual path.
    if(!CanMoveTo(x+xd, y+yd) || (!CanMoveTo(x,y+yd) && !CanMoveTo(x+xd,y)))
        { term << "You cannot go that way.\n"; return false; }

    long burden = eq.burden();

    if(pulling)
    {
        auto& room   = maze.GenerateRoom(x,y, defaultroom, 0);
        auto& target = maze.GenerateRoom(x+xd, y+yd, defaultroom, 0);

        const ItemReference what("all cart");

        long cart_burden = 0;
        for(long no=0; (no = room.items.find_item(what.refs.front(),no)) >= 0; )
        {
            cart_burden += (room.items.Items[no].cart->burden() + 10) / 5;

            // Can't use room.items.move() here, because technically
            // the cart is "immovable". Do the move manually.
            target.items.Items.push_front(room.items.Items[no]);
            room.items.Items.erase( room.items.Items.begin() + no );

            // Only pull the first cart.
            // The "push_front" above ensures that when coming to
            // a room with carts in it, you'll continue pulling the
            // same cart instead of switching to another one.
            break;
        }

        if(cart_burden)
        {
            //term << "Pulling that cart wears you down by %ld extra points.\n"_f % cart_burden;
            burden += cart_burden;
        }
    }

    x += xd;
    y += yd;
    EatLife(burden);

    return true;
}

static void Inv()
{
    auto p = eq.print(true);
    if(!p.second) term << "You are carrying nothing.\n";
    else          term << p.first << "\n";
}

static void LookAtIn(const Eq& where, const ItemReference& what,
                     const std::string& here_str = "here")
{
    // Look at items in the room.
    for(const auto& w: what.refs)
    {
        std::pair<std::string,float> output;
        auto AddItems = [&output](const std::pair<std::string,float>& src)
        {
            output.first  += src.first;
            output.second += src.second;
        };

        for(long no=0; (no = where.find_item(w,no)) >= 0; )
        {
            AddItems(where.look_item(no++,what.IsSpecific()));
            if(what.IsSpecific()) break;
        }

        // Look at money in the room, if there were no items or we're looking at everything.
        if(what.everything || output.first.empty())
            for(long no=0; (no = where.find_money(w,no)) >= 0; )
            {
                AddItems(where.look_money(no++,what.IsSpecific()));
                if(what.IsSpecific()) break;
            }

        if(here_str == "here")
        {
            bool room_empty = output.first.empty();

            // Look at inventory items, if there was nothing particular in the room.
            if(output.first.empty())
                for(long no=0; (no = eq.find_item(w,no)) >= 0; )
                {
                    AddItems(eq.look_item(no++,what.IsSpecific()));
                    if(what.IsSpecific()) break;
                }

            // Look at inventory money, if...
            if(output.first.empty() || (what.everything && room_empty))
                for(long no=0; (no = eq.find_money(w,no)) >= 0; )
                {
                    AddItems(eq.look_money(no++,what.IsSpecific()));
                    if(what.IsSpecific()) break;
                }
        }

        if(!what.IsSpecific() && !output.first.empty())
        {
            if(output.second < 1.f)
                output.first += "It is of no sales value at all.\n";
            else
                output.first += "You estimate that with them you could probably buy %s.\n"_f
                                % Appraise(output.second, 1, 1);
        }

        if(!output.first.empty())
            term << output.first;
        else
            if(what.IsSpecific())
                term << "There %s no %s %s that you can look at.\n"_f
                        % (w.what.back()=='s' ? "are" : "is")
                        % w.what
                        % here_str;
            else
                term << "There is nothing %s.\n"_f % here_str;
    }
}

static void LookAt(const ItemReference& what, const ItemReference& where)
{
    const Room &room = maze.GenerateRoom(x,y, defaultroom, 0);

    if(where.refs.empty())
        LookAtIn(room.items, what);
    else
    {
        unsigned n_sources = 0;
        for(const auto& w: where.refs)
        {
            unsigned n = 0;
            for(long no=0; (no = room.items.find_item(w,no)) >= 0; ++n)
            {
                const auto& container = room.items.Items[no++];
                if(container.chest > 0.f)
                {
                    term << "You cannot see inside a closed chest!\n";
                    continue;
                }
                if(!container.cart)
                {
                    term << UCfirst("%s does not contain anything!\n"_f
                                    % AddArticle(container.name(1,1), true));
                    continue;
                }
                LookAtIn(*container.cart, what,
                         "in %s"_f % AddArticle(container.name(0,1), true));
            }

            if(!n && room.items.find_money(w,0) >= 0)
            {
                term << "You cannot look inside money! They do not contain anything.\n";
                ++n;
            }
            if(!n && !where.everything)
                term << "Look where? There is no %s in this room!\n"_f % w.what;
            n_sources += n;
        }
        if(!n_sources && where.everything)
            term << "There is nothing in this room!\n";
    }
}

static void GetFrom(Eq& source, const ItemReference& what,
                    const std::string& from_str = "",
                    const std::string& here_str = "here")
{
    // Move stuff from room to the inventory.
    auto moved = source.move(eq, what);

    if(!moved.immovable.empty() && !what.everything)
        term << UCfirst("%s could not be moved!\n"_f
                        % ListWithCounts( std::move(moved.immovable) ));

    if(!moved.notfound.empty())
    {
        if(moved.notfound.empty())
            term << "There is nothing %s you can take!\n"_f % here_str;
        else
            term << "There is no %s %s!\n"_f
                    % ListWithCounts( std::move(moved.notfound) ) % here_str;
    }

    if(!moved.moved.empty())
    {
        auto num = moved.moved.size();
        std::string explanation = ListWithCounts( std::move(moved.moved) );
        term << "You take %s%s.\n"_f % explanation % from_str;
        // Eat two hitpoints for every item moved.
        EatLife(num * 2);
    }
    else
    {
        term << "Nothing taken%s.\n"_f % from_str;
    }
}

static void Get(const ItemReference& what, const ItemReference& where)
{
    Room &room = maze.GenerateRoom(x,y, defaultroom, 0);

    if(where.refs.empty())
        GetFrom(room.items, what);
    else
    {
        unsigned n_sources = 0;
        for(const auto& w: where.refs)
        {
            unsigned n = 0;
            for(long no=0; (no = room.items.find_item(w,no)) >= 0; ++n)
            {
                auto& container = room.items.Items[no++];
                if(container.chest > 0.f)
                {
                    term << "You cannot get anything from a closed chest.\n";
                    continue;
                }
                if(!container.cart)
                {
                    term << "You cannot take things from %s.\n"_f
                        % AddArticle(container.name(0,1), true);
                    continue;
                }
                GetFrom(*container.cart, what,
                        " from %s"_f % AddArticle(container.name(0,1), true),
                        "in %s"_f % AddArticle(container.name(0,1), true) );
            }
            if(!n && !where.everything)
                term << "Take from where? There is no %s in this room!\n"_f % w.what;
            n_sources += n;
        }
        if(!n_sources && where.everything)
            term << "There is nothing in this room!\n";
    }
}

static void PutTo(Eq& target, const ItemReference& what,
                  const std::string& targetname = "")
{
    // Move stuff from inventory to the specified destination.
    auto moved = eq.move(target, what);

    if(!moved.immovable.empty())
        term << UCfirst("%s could not be moved!\n"_f
                        % ListWithCounts( std::move(moved.immovable) ));

    if(!moved.notfound.empty())
    {
        if(moved.notfound.empty())
            term << "You don't have anything!\n";
        else
            term << "You don't have %s!\n"_f
                    % ListWithCounts( std::move(moved.notfound) );
    }

    if(!moved.moved.empty())
    {
        auto num = moved.moved.size();
        std::string explanation = ListWithCounts( std::move(moved.moved) );
        if(targetname.empty())
            term << "You drop %s.\n"_f % explanation;
        else
            term << "You put %s in %s.\n"_f % explanation % targetname;
        // Eat half hitpoint for every item dropped.
        EatLife(num / 2);
    }
    else
        term << "Nothing moved.\n";
}

static void Put(const ItemReference& what, const ItemReference& where)
{
    Room &room = maze.GenerateRoom(x,y, defaultroom, 0);

    if(where.refs.empty())
    {
        PutTo(room.items, what);
    }
    else
    {
        if(!where.IsSpecific())
        {
            term << "Put where exactly? \"%s\" is rather vague.\n"_f % where.original;
            return;
        }

        long container_no = room.items.find_item(where.refs.front());
        if(container_no < 0)
        {
            term << "Put in where? There is no %s in this room!\n"_f % where.original;
            return;
        }
        auto& container = room.items.Items[container_no];
        if(container.chest > 0.f)
        {
            term << "You cannot put things in a closed chest.\n";
            return;
        }
        if(!container.cart)
        {
            term << "You cannot put things in %s.\n"_f % AddArticle(container.name(0,1), true);
            return;
        }
        PutTo(*container.cart, what, AddArticle(container.name(0,1), true));
    }
}

static void Open(const ItemReference& what, const ItemReference& withwhat)
{
    Room &room = maze.GenerateRoom(x,y, defaultroom, 0);

    if(!what.IsSpecific())
    {
        term << "Open what exactly? \"%s\" is rather vague.\n"_f % what.original;
        return;
    }
    if(!withwhat.refs.empty() && !withwhat.IsSpecific())
    {
        term << "Use what exactly? \"%s\" is rather vague.\n"_f % withwhat.original;
        return;
    }

    long chest_no = room.items.find_item(what.refs.front());
    if(chest_no < 0)
    {
        term << "There is no %s to open in this room!\n"_f % what.original;
        return;
    }
    auto& open_item = room.items.Items[chest_no];
    if(open_item.chest <= 0.f)
    {
        term << UCfirst("%s is not particularly in need of opening.\n"_f
                        % AddArticle(open_item.name(1,1), true));
        return;
    }

    // If the player tries to use an implement to open the chest,
    // check whether they do in fact possess such a device.
    long item_no = -1;

    if(!withwhat.refs.empty())
    {
        item_no = eq.find_item(withwhat.refs.front());
        if(item_no < 0)
        {
            term << "You don't have any %s!\n"_f % withwhat.original;
            return;
        }
    }

    rnd.seed( 71161183 * room.seed + item_no + open_item.chest * 0x8088401
           + 971697*x + 5197161*y);

    // Evaluate the implement!
    // The heavier the material and the lighter the item, the more powerful it is.
    //        e.g. "iron tie" and "gold sceptre" are pretty good,
    //        and  "silk battlesuit" does not help at all.
    //   Our own power is equivalent to about "leather sceptre".
    // The heavier and less costly it is, the less damage it tends to take.
    //        e.g. "iron hammer" does not take much damage,
    //        but  "silk cap" is damaged beyond repair at once.
    //   This is not a perfect measure, for instance glass is more formidable as leather...
    //   We are about as formidable as "good leather dagger".
    // The heavier it is, the more effort it takes to use it.
    float prying_power      = 0.5 / 2.5;                    // 0.2
    float damage_resistance = (0.5 * 1.5) / (0.9*0.09*0.1); // 92.593
    long  effort_cost       = 8;

    if(item_no >= 0)
    {
        const auto& item = eq.Items[item_no];
        prying_power      = BuildTypes[item.build].weight / ItemTypes[item.type].weight;
        damage_resistance = item.weight() / item.value(1.f);
        effort_cost       = item.weight();
    }

    term << "You try to pry %s open %s.\n"_f
        % AddArticle(open_item.name(0,0), true)
        % (item_no < 0 ? "with your bare hands"
                       : ("using your " + eq.Items[item_no].name(1,1)));

    EatLife(effort_cost);

    open_item.chest -= prying_power * (0.5f + 5.f*std::pow(frand(),4.f));

    if(frand() > 0.75f && frand() > damage_resistance/500.f)
    {
        // If we are using an implement, the damage may
        // be transferred into the item instead!
        bool item_damaged = (item_no >= 0 && frand() >= 0.25f);
        if(item_damaged)
        {
            auto& item = eq.Items[item_no];
            std::string name = item.name(1,1);
            if(++item.condition >= count(CondTypes))
            {
                term << "`alert`Your %s gets damaged! It is utterly destroyed.\n"_f % name;
                eq.Items.erase( eq.Items.begin() + item_no );
            }
            else
            {
                term << "`alert`Your %s gets damaged! It is now in %s condition.\n"_f
                    % name
                    % item.GetCondition();
            }
        }
        else
        {
            unsigned sprain = rand(count(BodyParts));
            EatLife(BodyParts[sprain].worth);
            term << "You sprain your %s!\n"_f % BodyParts[sprain].name;
        }
    }

    term << "`reset`";

    // Did the chest open?
    if(open_item.chest > 0.f)
    {
        term << UCfirst("%s resists your meddling! Try harder.\n"_f
                        % AddArticle(open_item.name(0,0), true));
        if(item_no < 0 && frand() < 0.3)
            term << "Try using a tool: 'open chest using <item>'.\n";
        return;
    }

    open_item.chest = 1.0f; // to make sure the name is properly printed the last time
    term
        << UCfirst("%s bursts into pieces!\n"_f % AddArticle(open_item.name(0,0), true))
        << "Everything it contained is scattered on the ground.\n";

    // Delete the chest from the room.
    room.items.Items.erase( room.items.Items.begin() + chest_no );

    // Generate the contents of the box. There is at least one item inside.
    do
        if(frand() > 0.96) // pure money is rare.
        {
            unsigned moneytype((1.0-std::pow(frand(), 4)) * (count(MoneyTypes)-1));
            room.items.Money[moneytype] += rand(1600/MoneyTypes[moneytype].worth);
        }
        else
            room.items.Items.push_front(ItemType());
    while(frand() > 0.3);
}

struct Alias
{
    std::regex  pattern;
    std::string replacement;
} static const aliases[] =
{
    { R"(^l\b)"_r,                     "look"     },
    { R"(^lat? )"_r,                   "look at " },
    { R"(^lin? )"_r,                   "look in " },
    { R"(^look in )"_r,                "look at all in " },
    { R"(^ga\b)"_r,                    "get all"  },
    { R"(^da\b)"_r,                    "drop all" },
    { R"(^d )"_r,                      "drop "    },
    { R"(^g )"_r,                      "get "     },
    { R"(^take )"_r,                   "get "     },
    { R"(^pry )"_r,                    "open "    },
    { R"(^i\b)"_r,                     "inv"      },
    { R"(^inventory\b)"_r,             "inv"      },
    { R"(^da\b)"_r,                    "drop all" },
    { R"(^put(.*)\b(in|into|to)\b)"_r, "drop$1in" },
    { R"(\busing\b)"_r,                "with"     },
    { R"(\bwith my\b)"_r,              "with"     },
    { R"(^\s+)"_r,                     ""         },
    { R"(\s+$)"_r,                     ""         }
};

// A command line history and input engine.
struct CommandReader
{
    enum : unsigned { HistLen = 10, HistMin = 5 };

    std::deque<std::string> history;
    std::string prompt;
    std::pair<std::string, unsigned> repeat;

    void SetPrompt(const std::string& s) { prompt = s; }

    std::string ReadCommand()
    {
        for(;;)
        {
            term << "`prompt`%s`reset``flush`"_f % prompt;

            std::string cmd;

            if(repeat.second)
            {
                --repeat.second;
                cmd = repeat.first;
            }
            else
            {
                std::getline(std::cin, cmd);
                if(!std::cin.good()) return "quit";
            }
            if(cmd.empty()) continue;

            // Check if the command begins with a number, indicating
            // a desire to repeat a command a number of times.
            std::smatch res;
            if(std::regex_match(cmd, res, "^([1-9][0-9]*) +([^ 1-9].*)"_r))
            {
                repeat = { res[2], std::stoi(res[1]) };
                if(repeat.second > 50)
                {
                    term << "Ignoring too large repeat count %u\n"_f % repeat.second;
                    repeat.second = 0;
                }
                continue;
            }

            // Add every command to the history
            if(cmd[0] != '!' && !repeat.second && cmd.size() >= HistMin)
            {
                history.push_back(cmd);
                if(history.size() > HistLen) history.pop_front();
            }

            // Deal with history searches
            if(cmd[0] == '!' && cmd != "!?")
            {
                for(std::size_t a=history.size(); a-- > 0; )
                    if(history[a].compare(0, cmd.size()-1, cmd, 1, cmd.size()-1)==0)
                    {
                        term << "Repeating <%s>\n"_f % history[a];
                        cmd = history[a];
                        break;
                    }
                if(cmd[0] == '!') term << "No match found for (%s) from command history.\n"_f % cmd.substr(1);
                if(cmd[0] == '!') continue;
            }

            // Apply command aliases after dealing with the history
            for(;;)
            {
                std::string orig_cmd = cmd;
                for(const auto& r: aliases)
                    cmd = std::regex_replace(cmd, r.pattern, r.replacement);
                if(cmd == orig_cmd) break;
            }
            return cmd;
        }
    }
    void PrintHistory()
    {
        // Produce out the history of commands:
        term << "`reset`Your latest commands of at least %d characters:\n"_f % int(HistMin);
        for(std::size_t a=0; a<history.size(); ++a)
            term << "%3d : %s\n"_f % (a+1) % history[a];
    }
};

int main()
{
    term << "`reset`Welcome to the treasure dungeon.\n\n";

    CommandReader cmd;
help:
    term <<
        "`reset`Available commands:\n"
        "\tl/look\n"
        "\tla/look at <item>\n"
        "\tn/s/w/e for moving\n"
        "\tget <item>/get all/ga for short\n"
        "\tdrop <item>/drop all\n"
        "\ti/inv/inventory\n"
        "\tansi off, if the colors don't work for you\n"
        "\tquit\n"
        "\thelp\n\n"
        "You are starving. You are trying to find enough stuff to sell\n"
        "for food before you die. Beware, food is very expensive here.\n\n";

    // The main loop.
    Look();
    while(life > 0)
    {
        cmd.SetPrompt( "[life:%ld]> "_f % life );

        auto s = cmd.ReadCommand();
        // Produce the prompt and wait for player's command.
        if(s == "quit") break;
        if(s.empty()) continue;

        // Parse the command using C++11 regex.
        std::smatch res;

        #define rm std::regex_match

        // First, some metacommands
        if(s == "!?" || s == "history")     cmd.PrintHistory();
        else if(rm(s, R"((?:help|what|\?))"_r)) goto help;

        // Some fundamental movement commands
        else if(rm(s, "((go|walk|move) +)?(n|north)"_r)) { if(TryMoveBy( 0,-1)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(s|south)"_r)) { if(TryMoveBy( 0, 1)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(w|west)"_r))  { if(TryMoveBy(-1, 0)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(e|east)"_r))  { if(TryMoveBy( 1, 0)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(nw|northwest)"_r)) { if(TryMoveBy(-1,-1)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(ne|northeast)"_r)) { if(TryMoveBy( 1,-1)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(sw|southwest)"_r)) { if(TryMoveBy(-1, 1)) Look(); }
        else if(rm(s, "((go|walk|move) +)?(se|southeast)"_r)) { if(TryMoveBy( 1, 1)) Look(); }

        // Then commands for looking at things.
        // Use the power of regex to recognize complex syntax.
        else if(rm(s, "look( +around)?"_r)) Look();
        else if(rm(s, res, "look(?: +at)? +(.*?)(?: +in +(.+))?"_r)) LookAt(res[1].str(), res[2].str());

        // A command for opening chests, possibly with some implements
        else if(rm(s, res, "open +(.+?)(?: +with +(.+))?"_r))       Open(res[1].str(), res[2].str());
        else if(rm(s, "open|get|drop"_r))                           term << "%s what?\n"_f % s;

        // Inventory manipulation commands
        else if(s == "inv")                                         Inv();
        else if(rm(s, res, "get +(.+?)(?: +from +(.+))?"_r))        Get(res[1].str(), res[2].str());
        else if(rm(s, res, "drop +(.+?)(?: +(?:to|in) +(.+))?"_r))  Put(res[1].str(), res[2].str());

        else if(rm(s, res, "ansi +(off|on)"_r))  term.EnableDisable(res[1]=="on");
        else if(rm(s, R"((?:wear|wield|eq)\b.*)"_r))
            term << "You are scavenging for survival and not playing an RPG character.\n";
        else if(rm(s, R"(eat\b.*)"_r))
            term << "You have nothing edible! You are hoping to collect something you can sell for food.\n";
        else if(rm(s, R"(pull\b.*)"_r))
            { term << "Ok, you will pull any cart with you when you move. Type 'stop' to stop pulling.\n"; pulling = true; }
        else if(s == "stop")
            { term << "Ok, you will leave carts alone.\n"; pulling = false; }

        // Any unrecognized command.
        else term << "what?\n";
    }

    // By mercy, get all from cart.
    if(pulling) Get("all", "all cart");

    float value = eq.value();

    term
        << "`alert`%s\n"_f % (life<0
            ? "You are pulled out from the maze by a supernatural force!"
            : "byebye")
        << "[life:%ld] Game over\n`reset`"_f % life
        << "You managed to collect stuff worth %.2f gold.\n"_f % value
        << "With all your possessions, you purchase %s.\n"
           "You consume your reward eagerly.\n"_f % Appraise(value)
        << "YOU %s\n"_f
        % (value<10000.0
            ? "DID NOT SURVIVE. Hint: Learn to judge the value/weight ratio."
            : "SURVIVED! CONGRATULATION. ;)");
}


