#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

/**
 * PlanNodeType: Enumerates the physical and logical execution operators.
 */
enum class PlanNodeType {
    SEQ_SCAN,
    INDEX_SCAN,
    FILTER,
    NESTED_LOOP_JOIN,
    HASH_JOIN
};

/**
 * AbstractPlanNode: Base class for query execution plan trees produced by the optimizer.
 */
class AbstractPlanNode {
public:
    virtual ~AbstractPlanNode() = default;

    virtual PlanNodeType GetType() const = 0;
    virtual double GetCost() const = 0;
    virtual size_t GetEstimatedCardinality() const = 0;
    virtual std::vector<std::shared_ptr<AbstractPlanNode>> GetChildren() const = 0;
    virtual std::string ToString(int indent = 0) const = 0;

protected:
    static std::string MakeIndent(int indent) {
        return std::string(indent * 2, ' ');
    }
};

/**
 * SeqScanPlanNode: Physical plan node representing sequential scan over TableHeap.
 */
class SeqScanPlanNode : public AbstractPlanNode {
public:
    SeqScanPlanNode(std::string table_name, double cost, size_t cardinality)
        : table_name_(std::move(table_name)), cost_(cost), cardinality_(cardinality) {}

    PlanNodeType GetType() const override { return PlanNodeType::SEQ_SCAN; }
    double GetCost() const override { return cost_; }
    size_t GetEstimatedCardinality() const override { return cardinality_; }
    const std::string &GetTableName() const { return table_name_; }

    std::vector<std::shared_ptr<AbstractPlanNode>> GetChildren() const override {
        return {};
    }

    std::string ToString(int indent = 0) const override {
        std::stringstream ss;
        ss << MakeIndent(indent) << "-> SeqScan(" << table_name_ << ") [Cost: "
           << std::fixed << std::setprecision(2) << cost_ << ", Rows: " << cardinality_ << "]";
        return ss.str();
    }

private:
    std::string table_name_;
    double cost_{0.0};
    size_t cardinality_{0};
};

/**
 * IndexScanPlanNode: Physical plan node representing index scan over B+ Tree.
 */
class IndexScanPlanNode : public AbstractPlanNode {
public:
    IndexScanPlanNode(std::string table_name, std::string index_name,
                      double low_key, double high_key, double cost, size_t cardinality)
        : table_name_(std::move(table_name)), index_name_(std::move(index_name)),
          low_key_(low_key), high_key_(high_key), cost_(cost), cardinality_(cardinality) {}

    PlanNodeType GetType() const override { return PlanNodeType::INDEX_SCAN; }
    double GetCost() const override { return cost_; }
    size_t GetEstimatedCardinality() const override { return cardinality_; }
    const std::string &GetTableName() const { return table_name_; }
    const std::string &GetIndexName() const { return index_name_; }

    std::vector<std::shared_ptr<AbstractPlanNode>> GetChildren() const override {
        return {};
    }

    std::string ToString(int indent = 0) const override {
        std::stringstream ss;
        ss << MakeIndent(indent) << "-> IndexScan(" << table_name_ << " via " << index_name_
           << " [" << low_key_ << ".." << high_key_ << "]) [Cost: "
           << std::fixed << std::setprecision(2) << cost_ << ", Rows: " << cardinality_ << "]";
        return ss.str();
    }

private:
    std::string table_name_;
    std::string index_name_;
    double low_key_{0.0};
    double high_key_{0.0};
    double cost_{0.0};
    size_t cardinality_{0};
};

/**
 * FilterPlanNode: Physical plan node representing predicate filtering.
 */
class FilterPlanNode : public AbstractPlanNode {
public:
    FilterPlanNode(std::shared_ptr<AbstractPlanNode> child, std::string predicate_desc,
                   double cost, size_t cardinality)
        : child_(std::move(child)), predicate_desc_(std::move(predicate_desc)),
          cost_(cost), cardinality_(cardinality) {}

    PlanNodeType GetType() const override { return PlanNodeType::FILTER; }
    double GetCost() const override { return cost_; }
    size_t GetEstimatedCardinality() const override { return cardinality_; }
    const std::string &GetPredicate() const { return predicate_desc_; }
    std::shared_ptr<AbstractPlanNode> GetChild() const { return child_; }

    std::vector<std::shared_ptr<AbstractPlanNode>> GetChildren() const override {
        return {child_};
    }

    std::string ToString(int indent = 0) const override {
        std::stringstream ss;
        ss << MakeIndent(indent) << "-> Filter(" << predicate_desc_ << ") [Cost: "
           << std::fixed << std::setprecision(2) << cost_ << ", Rows: " << cardinality_ << "]\n";
        if (child_) {
            ss << child_->ToString(indent + 1);
        }
        return ss.str();
    }

private:
    std::shared_ptr<AbstractPlanNode> child_;
    std::string predicate_desc_;
    double cost_{0.0};
    size_t cardinality_{0};
};

/**
 * JoinPlanNode: Physical plan node representing binary joins (Nested Loop Join or Hash Join).
 */
class JoinPlanNode : public AbstractPlanNode {
public:
    JoinPlanNode(PlanNodeType join_type,
                 std::shared_ptr<AbstractPlanNode> left,
                 std::shared_ptr<AbstractPlanNode> right,
                 std::string join_key,
                 double cost, size_t cardinality)
        : join_type_(join_type), left_(std::move(left)), right_(std::move(right)),
          join_key_(std::move(join_key)), cost_(cost), cardinality_(cardinality) {}

    PlanNodeType GetType() const override { return join_type_; }
    double GetCost() const override { return cost_; }
    size_t GetEstimatedCardinality() const override { return cardinality_; }
    const std::string &GetJoinKey() const { return join_key_; }
    std::shared_ptr<AbstractPlanNode> GetLeft() const { return left_; }
    std::shared_ptr<AbstractPlanNode> GetRight() const { return right_; }

    std::vector<std::shared_ptr<AbstractPlanNode>> GetChildren() const override {
        return {left_, right_};
    }

    std::string ToString(int indent = 0) const override {
        std::stringstream ss;
        std::string type_str = (join_type_ == PlanNodeType::HASH_JOIN) ? "HashJoin" : "NestedLoopJoin";
        ss << MakeIndent(indent) << "-> " << type_str << "(ON " << join_key_ << ") [Cost: "
           << std::fixed << std::setprecision(2) << cost_ << ", Rows: " << cardinality_ << "]\n";
        if (left_) {
            ss << left_->ToString(indent + 1) << "\n";
        }
        if (right_) {
            ss << right_->ToString(indent + 1);
        }
        return ss.str();
    }

private:
    PlanNodeType join_type_;
    std::shared_ptr<AbstractPlanNode> left_;
    std::shared_ptr<AbstractPlanNode> right_;
    std::string join_key_;
    double cost_{0.0};
    size_t cardinality_{0};
};
