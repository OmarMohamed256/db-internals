#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

/**
 * Supported SQL Statement Types in Sprint 1.7
 */
enum class StatementType {
    INVALID,
    BEGIN,
    COMMIT,
    ABORT,
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CHECKPOINT,
    HELP,
    EXIT
};

/**
 * ParsedStatement stores extracted metadata, table names, values, and predicates.
 */
struct ParsedStatement {
    StatementType type{StatementType::INVALID};
    std::string table_name;
    
    // For INSERT INTO users VALUES (id, 'name', balance)
    int64_t insert_id{0};
    std::string insert_name;
    double insert_balance{0.0};
    
    // For SELECT / UPDATE / DELETE WHERE predicates
    bool has_where{false};
    std::string where_column;
    std::string where_op; // "=", ">", "<"
    int64_t where_id_val{0};
    double where_num_val{0.0};
    
    // For UPDATE users SET balance = <val> WHERE id = <id>
    std::string set_column;
    double set_val{0.0};

    // Raw input query
    std::string raw_query;
    std::string error_message;
};

/**
 * SQLParser: Zero-dependency lexical tokenizer and command extractor.
 * 
 * Supported grammar:
 *   - BEGIN; | COMMIT; | ABORT; | ROLLBACK;
 *   - INSERT INTO users VALUES (<id>, '<name>', <balance>);
 *   - SELECT * FROM users [WHERE id = <id> | WHERE balance > <num> | WHERE balance < <num>];
 *   - UPDATE users SET balance = <val> WHERE id = <id>;
 *   - DELETE FROM users WHERE id = <id>;
 *   - CHECKPOINT; | HELP; | EXIT; | QUIT;
 */
class SQLParser {
public:
    /**
     * Parses a raw SQL string into a structured ParsedStatement.
     */
    static ParsedStatement Parse(const std::string &sql) {
        ParsedStatement stmt;
        stmt.raw_query = sql;

        // 1. Trim leading/trailing whitespace and strip trailing ';'
        std::string cleaned = Trim(sql);
        if (cleaned.empty()) {
            stmt.type = StatementType::INVALID;
            return stmt;
        }
        if (cleaned.back() == ';') {
            cleaned.pop_back();
            cleaned = Trim(cleaned);
        }

        // 2. Tokenize string into whitespace/symbol separated tokens
        std::vector<std::string> tokens = Tokenize(cleaned);
        if (tokens.empty()) {
            stmt.type = StatementType::INVALID;
            return stmt;
        }

        std::string command = ToUpper(tokens[0]);


        return stmt;
    }

private:
    static std::string Trim(const std::string &str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string ToUpper(const std::string &str) {
        std::string res = str;
        std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
            return std::toupper(c);
        });
        return res;
    }

    static std::vector<std::string> Tokenize(const std::string &str) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_quotes = false;

        for (size_t i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (c == '\'') {
                in_quotes = !in_quotes;
                current += c;
            } else if (in_quotes) {
                current += c;
            } else if (c == '(' || c == ')' || c == ',' || c == '=' || c == '>' || c == '<') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(std::string(1, c));
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        return tokens;
    }
};
