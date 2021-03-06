#include "stdafx.h"
#include "opencv2/calib3d.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/core/utility.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fstream>
using namespace cv;
using namespace std;


static void
Stereo_Calib(const vector<string>& imagelist, Size boardSize, float squareSize, bool displayCorners = false, bool useCalibrated = true, bool showRectified = true)
{
	if (imagelist.size() % 2 != 0)
	{
		cout << "Error: the image list contains odd (non-even) number of elements\n";
		return;
	}

	const int maxScale = 2;
	// ARRAY AND VECTOR STORAGE:

	vector<vector<Point2f> > imagePoints[2];
	vector<vector<Point3f> > objectPoints;
	Size imageSize;

	int i, j, k, nimages = (int)imagelist.size() / 2;

	imagePoints[0].resize(nimages);
	imagePoints[1].resize(nimages);
	vector<string> goodImageList;
	for (i = j = 0; i < nimages; i++)
	{
		for (k = 0; k < 2; k++)
		{
			const string& filename = imagelist[i * 2 + k];
			Mat img = imread(filename, 0);
			if (img.empty())
				break;
			if (imageSize == Size())
				imageSize = img.size();
			else if (img.size() != imageSize)
			{
				cout << "The image " << filename << " has the size different from the first image size. Skipping the pair\n";
				break;
			}
			bool found = false;
			vector<Point2f>& corners = imagePoints[k][j];
			for (int scale = 1; scale <= maxScale; scale++)
			{
				Mat timg;
				if (scale == 1)
					timg = img;
				else
					resize(img, timg, Size(), scale, scale, INTER_LINEAR_EXACT);
				found = findChessboardCorners(timg, boardSize, corners,
					CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE);
				if (found)
				{
					if (scale > 1)
					{
						Mat cornersMat(corners);
						cornersMat *= 1. / scale;
					}
					break;
				}
			}
			if (displayCorners)
			{
				cout << filename << endl;
				Mat cimg, cimg1;
				cvtColor(img, cimg, COLOR_GRAY2BGR);
				drawChessboardCorners(cimg, boardSize, corners, found);
				double sf = 640. / MAX(img.rows, img.cols);
				resize(cimg, cimg1, Size(), sf, sf, INTER_LINEAR_EXACT);
				imshow("corners", cimg1);
				char c = (char)waitKey(500);
				if (c == 27 || c == 'q' || c == 'Q') //Allow ESC to quit
					exit(-1);
			}
			else
				putchar('.');
			if (!found)
				break;
			cornerSubPix(img, corners, Size(11, 11), Size(-1, -1),
				TermCriteria(TermCriteria::COUNT + TermCriteria::EPS,
					30, 0.01));
		}
		if (k == 2)
		{
			goodImageList.push_back(imagelist[i * 2]);
			goodImageList.push_back(imagelist[i * 2 + 1]);
			j++;
		}
	}
	cout << j << " pairs have been successfully detected.\n";
	nimages = j;
	if (nimages < 2)
	{
		cout << "Error: too little pairs to run the calibration\n";
		return;
	}

	imagePoints[0].resize(nimages);
	imagePoints[1].resize(nimages);
	objectPoints.resize(nimages);

	for (i = 0; i < nimages; i++)
	{
		for (j = 0; j < boardSize.height; j++)
			for (k = 0; k < boardSize.width; k++)
				objectPoints[i].push_back(Point3f(k*squareSize, j*squareSize, 0));
	}

	cout << "Running stereo calibration ...\n";

	Mat cameraMatrix[2], distCoeffs[2];
	cameraMatrix[0] = initCameraMatrix2D(objectPoints, imagePoints[0], imageSize, 0);
	cameraMatrix[1] = initCameraMatrix2D(objectPoints, imagePoints[1], imageSize, 0);
	Mat R, T, E, F;

	double rms = stereoCalibrate(objectPoints, imagePoints[0], imagePoints[1],
		cameraMatrix[0], distCoeffs[0],
		cameraMatrix[1], distCoeffs[1],
		imageSize, R, T, E, F,
		CALIB_FIX_ASPECT_RATIO +
		CALIB_ZERO_TANGENT_DIST +
		CALIB_USE_INTRINSIC_GUESS +
		CALIB_SAME_FOCAL_LENGTH +
		CALIB_RATIONAL_MODEL +
		CALIB_FIX_K3 + CALIB_FIX_K4 + CALIB_FIX_K5,
		TermCriteria(TermCriteria::COUNT + TermCriteria::EPS, 100, 1e-5));
	cout << "done with RMS error=" << rms << endl;

	// CALIBRATION QUALITY CHECK
	// because the output fundamental matrix implicitly
	// includes all the output information,
	// we can check the quality of calibration using the
	// epipolar geometry constraint: m2^t*F*m1=0
	double err = 0;
	int npoints = 0;
	vector<Vec3f> lines[2];
	for (i = 0; i < nimages; i++)
	{
		int npt = (int)imagePoints[0][i].size();
		Mat imgpt[2];
		for (k = 0; k < 2; k++)
		{
			imgpt[k] = Mat(imagePoints[k][i]);
			undistortPoints(imgpt[k], imgpt[k], cameraMatrix[k], distCoeffs[k], Mat(), cameraMatrix[k]);
			computeCorrespondEpilines(imgpt[k], k + 1, F, lines[k]);
		}
		for (j = 0; j < npt; j++)
		{
			double errij = fabs(imagePoints[0][i][j].x*lines[1][j][0] +
				imagePoints[0][i][j].y*lines[1][j][1] + lines[1][j][2]) +
				fabs(imagePoints[1][i][j].x*lines[0][j][0] +
					imagePoints[1][i][j].y*lines[0][j][1] + lines[0][j][2]);
			err += errij;
		}
		npoints += npt;
	}
	cout << "average epipolar err = " << err / npoints << endl;

	// save intrinsic parameters
	FileStorage fs("intrinsics.yml", FileStorage::WRITE);
	if (fs.isOpened())
	{
		fs << "M1" << cameraMatrix[0] << "D1" << distCoeffs[0] <<
			"M2" << cameraMatrix[1] << "D2" << distCoeffs[1];
		fs.release();
	}
	else
		cout << "Error: can not save the intrinsic parameters\n";

	Mat R1, R2, P1, P2, Q;
	Rect validRoi[2];

	stereoRectify(cameraMatrix[0], distCoeffs[0],
		cameraMatrix[1], distCoeffs[1],
		imageSize, R, T, R1, R2, P1, P2, Q,
		CALIB_ZERO_DISPARITY, 1, imageSize, &validRoi[0], &validRoi[1]);

	fs.open("extrinsics.yml", FileStorage::WRITE);
	if (fs.isOpened())
	{
		fs << "R" << R << "T" << T << "R1" << R1 << "R2" << R2 << "P1" << P1 << "P2" << P2 << "Q" << Q;
		fs.release();
	}
	else
		cout << "Error: can not save the extrinsic parameters\n";
}


static bool readStringList(const string& filename, vector<string>& l)
{
	l.resize(0);
	FileStorage fs(filename, FileStorage::READ);
	if (!fs.isOpened())
		return false;
	FileNode n = fs.getFirstTopLevelNode();
	if (n.type() != FileNode::SEQ)
		return false;
	FileNodeIterator it = n.begin(), it_end = n.end();
	for (; it != it_end; ++it)
		l.push_back((string)*it);
	return true;
}


int StereoCalib(int argc1, char** argv1)
{
	Size boardSize;
	string imagelistfn;
	bool showRectified;
	cv::CommandLineParser parser(argc1, argv1, "{w|7|}{h|5|}{s|3.4|}{nr||}{help||}{@input|stereo_calib.xml|}");
	if (parser.has("help"))
		return print_help();
	showRectified = !parser.has("nr");
	imagelistfn = parser.get<string>("@input");
	boardSize.width = parser.get<int>("w");
	boardSize.height = parser.get<int>("h");
	float squareSize = parser.get<float>("s");
	if (!parser.check())
	{
		parser.printErrors();
		return 1;
	}
	vector<string> imagelist;
	bool ok = readStringList(imagelistfn, imagelist);
	if (!ok || imagelist.empty())
	{
		cout << "can not open " << imagelistfn << " or the string list is empty" << endl;
		return print_help();
	}
	Stereo_Calib(imagelist, boardSize, squareSize, false, true, showRectified);
	return 0;
}


int StereoMatch(int argc2, char **argv2, Mat image1, Mat image2, Mat &disp8) {
	std::string img1_filename = "";
	std::string img2_filename = "";
	std::string intrinsic_filename = "";
	std::string extrinsic_filename = "";
	std::string disparity_filename = "";
	std::string point_cloud_filename = "";

	enum { STEREO_BM = 0, STEREO_SGBM = 1, STEREO_HH = 2, STEREO_VAR = 3, STEREO_3WAY = 4 };
	int alg = STEREO_SGBM;
	int SADWindowSize, numberOfDisparities;
	bool no_display;
	float scale;

	Ptr<StereoBM> bm = StereoBM::create(16, 9);
	Ptr<StereoSGBM> sgbm = StereoSGBM::create(0, 16, 3);
	cv::CommandLineParser parser(argc2, argv2,
		"{@arg1|test1.jpg|}{@arg2|test2.jpg|}{help h||}{algorithm|hh|}{max-disparity|32|}{blocksize|1|}{no-display|1|}{scale|1|}{i|intrinsics.yml|}{e| extrinsics.yml|}{o|display2.png|}{p|point.txt|}");

	//img1_filename = parser.get<std::string>(0);
	//img2_filename = parser.get<std::string>(1);
	if (parser.has("algorithm"))
	{
		std::string _alg = parser.get<std::string>("algorithm");
		alg = _alg == "bm" ? STEREO_BM :
			_alg == "sgbm" ? STEREO_SGBM :
			_alg == "hh" ? STEREO_HH :
			_alg == "var" ? STEREO_VAR :
			_alg == "sgbm3way" ? STEREO_3WAY : -1;
	}
	numberOfDisparities = parser.get<int>("max-disparity");
	SADWindowSize = parser.get<int>("blocksize");
	scale = parser.get<float>("scale");
	no_display = parser.has("no-display");
	if (parser.has("i"))
		intrinsic_filename = parser.get<std::string>("i");
	if (parser.has("e"))
		extrinsic_filename = parser.get<std::string>("e");
	if (parser.has("o"))
		disparity_filename = parser.get<std::string>("o");
	if (parser.has("p"))
		point_cloud_filename = parser.get<std::string>("p");
	if (!parser.check())
	{
		parser.printErrors();
		return 1;
	}


	int color_mode = alg == STEREO_BM ? 0 : -1;
	//Mat img1 = imread(img1_filename, color_mode);
	//Mat img2 = imread(img2_filename, color_mode);
	Mat img1 = image1;
	Mat img2 = image2;
	if (scale != 1.f)
	{
		Mat temp1, temp2;
		int method = scale < 1 ? INTER_AREA : INTER_CUBIC;
		resize(img1, temp1, Size(), scale, scale, method);
		img1 = temp1;
		resize(img2, temp2, Size(), scale, scale, method);
		img2 = temp2;
	}

	Size img_size = img1.size();

	Rect roi1, roi2;


	if (!intrinsic_filename.empty())
	{
		// reading intrinsic parameters
		FileStorage fs(intrinsic_filename, FileStorage::READ);

		if (!fs.isOpened())
		{
			printf("Failed to open file %s\n", intrinsic_filename.c_str());
			return -1;
		}

		Mat M1, D1, M2, D2;
		fs["M1"] >> M1;
		fs["D1"] >> D1;
		fs["M2"] >> M2;
		fs["D2"] >> D2;

		M1 *= scale;
		M2 *= scale;
		fs.open(extrinsic_filename, FileStorage::READ);
		if (!fs.isOpened())
		{
			printf("Failed to open file %s\n", extrinsic_filename.c_str());
			return -1;
		}

		Mat R, T, R1, P1, R2, P2;
		fs["R"] >> R;
		fs["T"] >> T;
		/*double r[3][3] = { 9.9926249029560554e-01, -1.1999216892283768e-02,
			-3.6475941142030260e-02, 1.0872695309321827e-02,
			9.9946250864896413e-01, -3.0926983393632347e-02,
			3.6827435220709775e-02, 3.0507582649093840e-02,
			9.9885585917887731e-01 };
		double t[3][1] = { -1.0542730892711472e+01, -5.6154386522713406e-01,-1.9350572481407355e-02 };
		double m1[3][3] = { 2.7488353436798279e+03, 0., 9.6362779673945238e+02, 0.,
			2.6062335968262491e+03, 5.3925652554700844e+02, 0., 0., 1. };
		double d1[1][14] = { 3.4692060131471646e-03, -3.3419543547059818e+00, 0., 0., 0.,
			0., 0., -2.8807569742181407e+01, 0., 0., 0., 0., 0., 0. };
		double m2[3][3] = { 2.7488353436798279e+03, 0., 9.4861406024296116e+02, 0.,
			2.6062335968262491e+03, 5.3600284248720368e+02, 0., 0., 1. };
		double d2[1][14] = { 6.2531509182270739e-01, -2.1338604801156471e+01, 0., 0., 0.,
			0., 0., -2.1651256477923195e+02, 0., 0., 0., 0., 0., 0. };

		Mat R(3, 3, CV_64F, r), T(3, 1, CV_64F, t), R1(3, 3, CV_64F), R2(3, 3, CV_64F), P1(3, 4, CV_64F),
			P2(3, 4, CV_64F), Q(4, 4, CV_64F), M1(3, 3, CV_64F, m1), M2(3, 3, CV_64F, m2),
			D1(1, 14, CV_64F, d1), D2(1, 14, CV_64F, d2), rmap[2][2];*/

		Mat R1(3, 3, CV_64F), R2(3, 3, CV_64F), P1(3, 4, CV_64F), P2(3, 4, CV_64F), Q(4, 4, CV_64F), rmap[2][2];

		stereoRectify(M1, D1, M2, D2, img_size, R, T, R1, R2, P1, P2, Q, CALIB_ZERO_DISPARITY, -1, img_size, &roi1, &roi2);

		Mat map11, map12, map21, map22;
		initUndistortRectifyMap(M1, D1, R1, P1, img_size, CV_16SC2, map11, map12);
		initUndistortRectifyMap(M2, D2, R2, P2, img_size, CV_16SC2, map21, map22);

		Mat img1r, img2r;
		remap(img1, img1r, map11, map12, INTER_LINEAR);
		remap(img2, img2r, map21, map22, INTER_LINEAR);

		img1 = img1r;
		img2 = img2r;


		numberOfDisparities = numberOfDisparities > 0 ? numberOfDisparities : ((img_size.width / 8) + 15) & -16;

		bm->setROI1(roi1);
		bm->setROI2(roi2);
		bm->setPreFilterCap(31);
		bm->setBlockSize(SADWindowSize > 0 ? SADWindowSize : 9);
		bm->setMinDisparity(0);
		bm->setNumDisparities(numberOfDisparities);
		bm->setTextureThreshold(10);
		bm->setUniquenessRatio(15);
		bm->setSpeckleWindowSize(100);
		bm->setSpeckleRange(32);
		bm->setDisp12MaxDiff(1);

		sgbm->setPreFilterCap(63);
		int sgbmWinSize = SADWindowSize > 0 ? SADWindowSize : 3;
		sgbm->setBlockSize(sgbmWinSize);

		int cn = img1.channels();

		sgbm->setP1(8 * cn*sgbmWinSize*sgbmWinSize);
		sgbm->setP2(32 * cn*sgbmWinSize*sgbmWinSize);
		sgbm->setMinDisparity(0);
		sgbm->setNumDisparities(numberOfDisparities);
		sgbm->setUniquenessRatio(10);
		sgbm->setSpeckleWindowSize(50);
		sgbm->setSpeckleRange(32);
		sgbm->setDisp12MaxDiff(1);
		if (alg == STEREO_HH)
			sgbm->setMode(StereoSGBM::MODE_HH);
		else if (alg == STEREO_SGBM)
			sgbm->setMode(StereoSGBM::MODE_SGBM);
		else if (alg == STEREO_3WAY)
			sgbm->setMode(StereoSGBM::MODE_SGBM_3WAY);

		Mat disp;
		//Mat img1p, img2p, dispp;
		//copyMakeBorder(img1, img1p, 0, 0, numberOfDisparities, 0, IPL_BORDER_REPLICATE);
		//copyMakeBorder(img2, img2p, 0, 0, numberOfDisparities, 0, IPL_BORDER_REPLICATE);

		if (alg == STEREO_BM)
			bm->compute(img1, img2, disp);
		else if (alg == STEREO_SGBM || alg == STEREO_HH || alg == STEREO_3WAY)
			sgbm->compute(img1, img2, disp);

		//disp = dispp.colRange(numberOfDisparities, img1p.cols);
		if (alg != STEREO_VAR)
			disp.convertTo(disp8, CV_8U, 255 / (numberOfDisparities*16.));
		else
			disp.convertTo(disp8, CV_8U);
		/*if (!no_display)
		{
			namedWindow("left", 1);
			imshow("left", img1);
			namedWindow("right", 1);
			imshow("right", img2);
			namedWindow("disparity", 0);
			imshow("disparity", disp8);
			//printf("press any key to continue...");
			fflush(stdout);
			waitKey(3000);
			//printf("\n");
		}*/
		//if (!disparity_filename.empty())
		//	imwrite(disparity_filename, disp8);
		return 0;
	}
}

void DispColorMap(const Mat &img, Mat &img_color)
{
	img_color = Mat(img.rows, img.cols, CV_8UC3);//RGB图像
#define IMG_B(img,y,x) img.at<Vec3b>(y,x)[0]
#define IMG_G(img,y,x) img.at<Vec3b>(y,x)[1]
#define IMG_R(img,y,x) img.at<Vec3b>(y,x)[2]
	uchar tmp2 = 0;
	cout << "color" << endl;
	for (int y = 0; y<img.rows; y++)//转为彩虹图的具体算法，主要思路是把灰度图对应的0～255的数值分别转换成彩虹色：红、橙、黄、绿、青、蓝。
	{
		for (int x = 0; x<img.cols; x++)
		{
			tmp2 = img.at<uchar>(y, x);
			if (tmp2 <= 51)
			{
				IMG_B(img_color, y, x) = 255;
				IMG_G(img_color, y, x) = 0;
				IMG_R(img_color, y, x) = 0;
			}
			else if (tmp2 <= 102)
			{
				tmp2 -= 51;
				IMG_B(img_color, y, x) = 255 - tmp2 * 15;
				IMG_G(img_color, y, x) = 255;
				IMG_R(img_color, y, x) = 0;
			}
			else if (tmp2 <= 153)
			{
				tmp2 -= 102;
				IMG_B(img_color, y, x) = 0;
				IMG_G(img_color, y, x) = 255;
				IMG_R(img_color, y, x) = tmp2 * 15;
			}
			else if (tmp2 <= 204)
			{
				tmp2 -= 153;
				IMG_B(img_color, y, x) = 0;
				IMG_G(img_color, y, x) = 255 - uchar(128.0*tmp2 / 51.0 + 0.5);
				IMG_R(img_color, y, x) = 255;
			}
			else
			{
				IMG_B(img_color, y, x) = 255;
				IMG_G(img_color, y, x) = 255;
				IMG_R(img_color, y, x) = 0;
			}
		}
	}
}

int main(int argc, char** argv){
    //StereoCalib(argc, argv);
	//system("PAUSE");

	//initialize and allocate memory to load the video stream from camera 
	VideoCapture camera0(1);
	camera0.open(1, CAP_DSHOW);
	VideoCapture camera1(0);
	camera1.open(0, CAP_DSHOW);
	Mat map,disp,disp8, frame0, frame1;
	if (!camera0.isOpened()) return 1;
	if (!camera1.isOpened()) return 1;
	namedWindow("disp", WINDOW_NORMAL);
	cvResizeWindow("disp", 500, 500);
	namedWindow("left", WINDOW_NORMAL);
	cvResizeWindow("left", 500, 500);
	int i=0;
	while (true) {
		//grab and retrieve each frames of the video sequentially 
		camera0.read(frame0);
		camera1.read(frame1);
		if (frame0.empty())
		{
			continue;
		}
		//Elas_Match(frame0, frame1, disp8);
		StereoMatch(argc, argv, frame1, frame0, disp8);
		DispColorMap(disp8, map);
		//disp.convertTo(disp8, CV_8U, 255 / (32 * 16.));
		imshow("disp", map);
		imshow("right", frame0);
	}
    return 0;
}
