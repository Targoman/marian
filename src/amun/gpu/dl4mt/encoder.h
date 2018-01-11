#pragma once

#include <yaml-cpp/yaml.h>

#include "common/sentence.h"
#include "common/enc_out.h"
#include "gpu/types-gpu.h"
#include "gpu/mblas/matrix_functions.h"
#include "model.h"
#include "gru.h"
#include "lstm.h"
#include "multiplicative.h"
#include "cell.h"
#include "cellstate.h"

namespace amunmt {

class Sentences;

namespace GPU {

class Encoder {
  private:
    template <class Weights>
    class Embeddings {
      public:
        Embeddings(const Weights& model)
        : w_(model)
        {}

        void Lookup(mblas::Matrix& Row, const std::vector<std::vector<Word>>& words) {
          std::vector<std::vector<unsigned>> knownWords(w_.Es_.size(),
                                                    std::vector<unsigned>(words.size(), 1));
          unsigned factorCount = w_.Es_.size();
          for (unsigned i = 0; i < words.size(); ++i) {
            const std::vector<Word>& factors = words[i];
            for (unsigned factorIdx = 0; factorIdx < factors.size(); ++factorIdx) {
              const Word& factor = factors[factorIdx];
              const std::shared_ptr<mblas::Matrix>& Emb = w_.Es_.at(factorIdx);

              if (factor < Emb->dim(0)) {
                knownWords[factorIdx][i] = factor;
              }
            }
          }

          unsigned wordCount = words.size() / factorCount;

          mblas::Vector<unsigned> dKnownWords;
          for (unsigned i = 0; i < knownWords.size(); i++) {
            const std::vector<unsigned>& factorWords = knownWords.at(i);
            dKnownWords.copyFrom(factorWords);

            const std::shared_ptr<mblas::Matrix>& Emb = w_.Es_.at(i);
            mblas::Matrix factorRow;
            factorRow.NewSize(wordCount, Emb->dim(1));
            mblas::Assemble(factorRow, *Emb, dKnownWords);
            mblas::Transpose(factorRow);

            if (i > 0) {
              mblas::Concat(Row, factorRow);
            } else {
              mblas::Copy(Row, factorRow);
            }

            /* eit++; */
            /* wit++; */
          }
          mblas::Transpose(Row);

        }

        unsigned FactorCount() {
          return w_.Es_.size();
        }

      private:
        const Weights& w_;

        Embeddings(const Embeddings&) = delete;
    };

    class RNN {
      public:
        RNN(std::unique_ptr<Cell> cell)
          : gru_(std::move(cell)) {}

        void InitializeState(unsigned batchSize = 1) {
          CellLength cellLength = gru_->GetStateLength();
          if (cellLength.cell > 0) {
            State_.cell->NewSize(batchSize, cellLength.cell);
            mblas::Fill(*(State_.cell), 0.0f);
          }
          State_.output->NewSize(batchSize, cellLength.output);
          mblas::Fill(*(State_.output), 0.0f);
        }

        void GetNextState(CellState& NextState,
                          const CellState& State,
                          const mblas::Matrix& Embd) {
          gru_->GetNextState(NextState, State, Embd);
        }

        template <class It>
        void Encode(It it, It end, mblas::Matrix& Context,
                    unsigned batchSize, bool invert,
                    const mblas::Vector<unsigned> *sentenceLengths=nullptr)
        {
          InitializeState(batchSize);

          CellState prevState(std::unique_ptr<mblas::Matrix>(new mblas::Matrix(*(State_.cell))),
                              std::unique_ptr<mblas::Matrix>(new mblas::Matrix(*(State_.output))));
          unsigned n = std::distance(it, end);
          unsigned i = 0;

          while(it != end) {
            GetNextState(State_, prevState, *it++);

            //std::cerr << "invert=" << invert << std::endl;
            if(invert) {
              assert(sentenceLengths);

              mblas::MapMatrix(*(State_.output), *sentenceLengths, n - i - 1);
              if (State_.cell->size()) {
                mblas::MapMatrix(*(State_.cell), *sentenceLengths, n - i - 1);
              }

              mblas::PasteRows(Context, *(State_.output), (n - i - 1), gru_->GetStateLength().output);
            }
            else {
              mblas::PasteRows(Context, *(State_.output), i, 0);
            }

            if (State_.cell->size() > 0) {
              prevState.cell->swap(*(State_.cell));
            }
            prevState.output->swap(*(State_.output));
            ++i;
          }
        }

        CellLength GetStateLength() const {
          return gru_->GetStateLength();
        }

      private:
        const std::unique_ptr<Cell> gru_;
        CellState State_;
        RNN(const RNN&) = delete;
    };

  public:
    Encoder(const Weights& model, const YAML::Node& config);

    void Encode(EncOutPtr encOut, unsigned tab);

  private:
    std::unique_ptr<Cell> InitForwardCell(const Weights& model, const YAML::Node& config);
    std::unique_ptr<Cell> InitBackwardCell(const Weights& model, const YAML::Node& config);

  private:
    Embeddings<Weights::EncEmbeddings> embeddings_;
    RNN forwardRnn_;
    RNN backwardRnn_;

    // reusing memory
    std::vector<mblas::Matrix> embeddedWords_;

    Encoder(const Encoder&) = delete;
};

}
}

