#include <gtest/gtest.h>
#include <utils/geom/Position2DVector.h>
#include <utils/common/UtilExceptions.h>

using namespace std;

/*
Tests the class Position2DVector
*/
class Position2DVectorTest : public testing::Test {
	protected :
		Position2DVector *vectorPolygon;
		Position2DVector *vectorLine;

		virtual void SetUp(){
			vectorPolygon = new Position2DVector();
			vectorPolygon->push_back(Position2D(0,0));
			vectorPolygon->push_back(Position2D(0,2));
			vectorPolygon->push_back(Position2D(2,4));
			vectorPolygon->push_back(Position2D(4,2));
			vectorPolygon->push_back(Position2D(4,0));	

			vectorLine = new Position2DVector();
			vectorLine->push_back(Position2D(0,0));
			vectorLine->push_back(Position2D(2,2));
		}

		virtual void TearDown(){
			delete vectorPolygon;
			delete vectorLine;
		}
};

/* Test the method 'around'*/
TEST_F(Position2DVectorTest, test_method_around) {

	EXPECT_TRUE(vectorPolygon->around(Position2D(1,1)));
	EXPECT_TRUE(vectorPolygon->around(Position2D(1,2)));
	EXPECT_FALSE(vectorPolygon->around(Position2D(4,4)));
	EXPECT_FALSE(vectorPolygon->around(Position2D(0,0)));

	EXPECT_FALSE(vectorLine->around(Position2D(1,1)));
	EXPECT_FALSE(vectorLine->around(Position2D(0,2)));
}

/* Test the method 'getPolygonCenter'.*/
TEST_F(Position2DVectorTest, test_method_getPolygonCenter) {
	Position2D pos = vectorPolygon->getPolygonCenter();
	EXPECT_FLOAT_EQ(2, pos.x());
	EXPECT_FLOAT_EQ(1.6, pos.y());
	Position2D pos2 = vectorLine->getPolygonCenter();
	EXPECT_FLOAT_EQ(1, pos2.x());
	EXPECT_FLOAT_EQ(1, pos2.y());
	
}

/* Test the method 'getBoxBoundary'*/
TEST_F(Position2DVectorTest, test_method_getBoxBoundary) {	
	Boundary bound = vectorPolygon->getBoxBoundary();
	EXPECT_FLOAT_EQ(bound.xmax(), 4);
	EXPECT_FLOAT_EQ(bound.xmin(), 0);
	EXPECT_FLOAT_EQ(bound.ymax(), 4);
	EXPECT_FLOAT_EQ(bound.ymin(), 0);
}

/* Test the method 'splitAt'*/
TEST_F(Position2DVectorTest, test_method_splitAt) {	
    Position2DVector vec;
    vec.push_back(Position2D(0,0));
    vec.push_back(Position2D(2,0));
    vec.push_back(Position2D(5,0));
    SUMOReal smallDiff = POSITION_EPS / 2;
    std::pair<Position2DVector, Position2DVector> result;
    // split in first segment
    result = vec.splitAt(1);
	EXPECT_FLOAT_EQ(2, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(1, result.first[1].x());
	EXPECT_FLOAT_EQ(3, result.second.size());
	EXPECT_FLOAT_EQ(1, result.second[0].x());
	EXPECT_FLOAT_EQ(2, result.second[1].x());
	EXPECT_FLOAT_EQ(5, result.second[2].x());
    // split in second segment
    result = vec.splitAt(4);
	EXPECT_FLOAT_EQ(3, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(2, result.first[1].x());
	EXPECT_FLOAT_EQ(4, result.first[2].x());
	EXPECT_FLOAT_EQ(2, result.second.size());
	EXPECT_FLOAT_EQ(4, result.second[0].x());
	EXPECT_FLOAT_EQ(5, result.second[1].x());
    // split close before inner point
    result = vec.splitAt(2 - smallDiff);
	EXPECT_FLOAT_EQ(2, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(2, result.first[1].x());
	EXPECT_FLOAT_EQ(2, result.second.size());
	EXPECT_FLOAT_EQ(2, result.second[0].x());
	EXPECT_FLOAT_EQ(5 ,result.second[1].x());
    // split close after inner point
    result = vec.splitAt(2 + smallDiff);
	EXPECT_FLOAT_EQ(2, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(2, result.first[1].x());
	EXPECT_FLOAT_EQ(2, result.second.size());
	EXPECT_FLOAT_EQ(2, result.second[0].x());
	EXPECT_FLOAT_EQ(5 ,result.second[1].x());

    // catch a bug
    vec.push_back(Position2D(6,0));
    vec.push_back(Position2D(8,0));
    // split at inner point
    result = vec.splitAt(5);
	EXPECT_FLOAT_EQ(3, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(2, result.first[1].x());
	EXPECT_FLOAT_EQ(5, result.first[2].x());
	EXPECT_FLOAT_EQ(3, result.second.size());
	EXPECT_FLOAT_EQ(5, result.second[0].x());
	EXPECT_FLOAT_EQ(6 ,result.second[1].x());
	EXPECT_FLOAT_EQ(8 ,result.second[2].x());

    // split short vector
    Position2DVector vec2;
    vec2.push_back(Position2D(0,0));
    vec2.push_back(Position2D(2,0));
    result = vec2.splitAt(1);
	EXPECT_FLOAT_EQ(2, result.first.size());
	EXPECT_FLOAT_EQ(0, result.first[0].x());
	EXPECT_FLOAT_EQ(1, result.first[1].x());
	EXPECT_FLOAT_EQ(2, result.second.size());
	EXPECT_FLOAT_EQ(1, result.second[0].x());
	EXPECT_FLOAT_EQ(2 ,result.second[1].x());

    // invalid split pos
    EXPECT_THROW(vec2.splitAt(smallDiff), InvalidArgument);
    EXPECT_THROW(vec2.splitAt(2 - smallDiff), InvalidArgument);
}
